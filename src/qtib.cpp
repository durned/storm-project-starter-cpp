#include "qtib.h"
#include <storm/api/storm.h>

bool stateRewards;
constexpr bool PRS_PRECOMPUTED = true;

std::vector<oneStepBelief>& computeOneStepBeliefs(const Pomdp& model,
    const std::vector<std::vector<uint32_t>>& observationStates,
    std::unordered_map<oneStepBelief, size_t, oneStepBeliefHash>& beliefIndices,
    std::vector<std::vector<size_t>>& stateOneStepBeliefs
) {
    const auto& transitionM = model.getTransitionMatrix();
    const auto& rowGroupIds = transitionM.getRowGroupIndices();
    const auto& stateObservations = model.getObservations();

    // Compute all (unique) one-step beliefs
    const auto rowGroupCount = transitionM.getRowGroupCount();
    static std::vector<oneStepBelief> oneStepBeliefs;

    // forall states
    for (uint64_t rgid = 0; rgid < rowGroupCount; rgid++) {
        // no actions from sink states should be taken,
        // so no one-step beliefs
        if (model.isSinkState(rgid)) {
            continue;
        }

        uint64_t until = rowGroupIds[rgid] + transitionM.getRowGroupSize(rgid);
        // forall actions
        for (auto rowId = rowGroupIds[rgid]; rowId < until; rowId++) {
            for (const auto& row = transitionM.getRow(rowId); const auto& entry : row) {
                // assuming only non-zero entries are stored
                const auto sPrime = entry.getColumn();

                // assuming one observation per state
                const uint64_t obsInSPrime = stateObservations[sPrime];

                oneStepBelief belief = {rgid, rowId - rowGroupIds[rgid], obsInSPrime, row};
                // one-step beliefs must be unique
                if (auto [_, inserted] = beliefIndices.emplace(belief, oneStepBeliefs.size()); inserted) {
                    stateOneStepBeliefs[belief.s].push_back(oneStepBeliefs.size());
                    oneStepBeliefs.push_back(belief);
                }
            }
        }
    }

    /* Append a BitVector with reachable one-step beliefs (osb) to
    each one-step belief. stateOneStepBeliefs holds all states and
    which osb begin in it, i.e. s = b_s. For reachability of a b_{s,a,o}:
    1. Retrieve row (s,a) from the transition matrix.
    2. For each s' in the row, see whether o matches observation in s'.
    3. Include all osb beginning in s'.
    */
    const auto numOSB = oneStepBeliefs.size();
    for (auto& osb : oneStepBeliefs) {
        std::vector<uint64_t> setEntries;

        for (uint64_t rowId = rowGroupIds[osb.s]+osb.a; auto& entry : transitionM.getRow(rowId)) {
            auto sPrime = entry.getColumn();
            if (stateObservations[sPrime] == osb.o) {
                for (auto osbIdx : stateOneStepBeliefs[sPrime]) {
                    setEntries.push_back(osbIdx);
                }
            }
        }

        osb.reachableBeliefs = storm::storage::BitVector(numOSB, setEntries);
    }

    /* Pre-compute state probabilities for each osb:
     * For each (unique) state amongst all states from
     * reachable osb call the bOfS method
    */
    if (PRS_PRECOMPUTED) {
        for (auto& osb : oneStepBeliefs) {
            probDist stateProbs;

            for (auto it = osb.reachableBeliefs.begin(); it != osb.reachableBeliefs.end(); ++it) {
                auto s = oneStepBeliefs[*it].s;

                auto probIter = stateProbs.find(s);
                if (probIter == stateProbs.end()) {
                    // key does not exist yet,
                    // compute and put
                    stateProbs.emplace(s, osb.bOfS(s, stateObservations));
                }
            }

            osb.stateProbs = stateProbs;
        }
    }

    return oneStepBeliefs;
}

// This function returns an array with the number of actions as values,
// and with observations acting as indexes. Since both are just indices in storm,
// this should allow for easy mapping. In TIB loops the set of actions is used,
// and, since I am working with a transition matrix and not set theory, I need to know what
// I am iterating over in for loops. Specifically, this would define the set of actions
// in the Cartesian product 'B_1 X A' used in TIB. The "observations" part is explained below.
std::vector<uint64_t>& getNumOfActionsForObservations(Pomdp& model) {
    const auto S = model.getNumberOfStates();
    const auto O = model.getNrObservations();

    const auto& stateObservations = model.getObservations();
    const auto& transitionM = model.getTransitionMatrix();

    static std::vector<uint64_t> numOfActions(O); // O is the number of observations, not zero

    for (uint64_t o = 0; o < O; o++) {
        // look for a state which has this observation
        for (uint_fast64_t s = 0; s < S; s++) {
            if (stateObservations[s] == o) {
                if (model.isSinkState(s)) {
                    numOfActions[o] = 0;
                    break;
                }
                // All states with the same observation share the same actions,
                // so I save the number of rows (actions) in a rowGroup for set s
                numOfActions[o] = transitionM.getRowGroupSize(s);
                break;
            }
        }
    }

    return numOfActions;
}

// should be pre-computed
double PrOfObs(const uint64_t obs, const storm::storage::SparseMatrix<double>::const_rows& saRow, const std::vector<uint32_t>& stateObs) {
    double res = 0;
    for (auto& entry : saRow) {
        auto sPrime = entry.getColumn();

        if (stateObs[sPrime] == obs) {
            res += entry.getValue();
        }
    }

    // printf("Pr(%lu): %.2f\n", obs, res);
    return res;
}

// should be pre-computed
double beliefActionReward(const oneStepBelief& belief, const uint64_t action,
    const std::vector<storm::storage::SparseMatrix<double>::index_type>& rowGroupIds,
    const storm::models::sparse::StandardRewardModel<double>& rewardModel,
    const std::vector<uint32_t>& stateObs, const std::vector<uint32_t>& states) {
    double res = 0;

    for (const auto s : states) {
        double reward = rewardModel.getStateActionReward(rowGroupIds[s]+action);
        if (stateRewards) {
            reward += rewardModel.getStateReward(s);
        }

        double bOfS;
        if (PRS_PRECOMPUTED) {
            bOfS = belief.stateProbs.count(s) ? belief.stateProbs.at(s) : 0.0;
            /*
            if (bOfS == 0.0) {
                printf("catch-all\n");
            }
            */
        } else {
            bOfS = belief.bOfS(s, stateObs);
        }

        res += bOfS * reward;
    }

    return res;
}

double Q_TIB(std::shared_ptr<Pomdp> model, const std::string& func, const int iterations, const double discount, const double epsilon) {
    assert(iterations > 0);
    assert(func == MIN || func == MAX);

    assert(model->hasUniqueRewardModel());
    const auto& rewardModel = model->getRewardModels().begin()->second;

    assert(rewardModel.hasStateActionRewards());

    if (rewardModel.hasStateRewards()) {
        stateRewards = true;
    } else {
        stateRewards = false;
    }

    const auto S = model->getNumberOfStates();
    const auto O = model->getNrObservations();

    printf("S=%lu\nO=%lu\nC=%lu\n\n", S, O, model->getNumberOfChoices());

    const auto& initStates = model->getInitialStates();
    const auto nOfInitStates = static_cast<double>(initStates.getNumberOfSetBits());
    printf("N_initStates=%.0f\n", nOfInitStates);

    const auto& stateObservations = model->getObservations();
    // map observations to set of states which have them
    std::vector<std::vector<uint32_t>> observationStates(O);
    for (uint_fast64_t s = 0; s < S; s++) {
        const auto o = stateObservations[s];
        observationStates[o].push_back(s);
    }

    const auto& transitionM = model->getTransitionMatrix();
    const auto& rowGroupIds = transitionM.getRowGroupIndices();

    // Eq. (2)

    // initial belief
    // double p = probDist.count(s) ? probDist.at(s) : 0.0;
    probDist b0;
    for (auto it = initStates.begin(); it != initStates.end(); ++it) {
        b0[*it] = 1.0 / nOfInitStates;
    }
    const auto sampleInitState = b0.begin()->first;
    const auto initObs = stateObservations[sampleInitState];

    printf("b_0:\t\t");
    for (const auto& [key, val] : b0) {
        printf("s=%lu Pr=%.2f\t\t", key, val);
    }
    printf("\n\n");

    // rest of one-step beliefs
    std::unordered_map<oneStepBelief, size_t, oneStepBeliefHash> beliefIndices;
    std::vector<std::vector<size_t>> stateOneStepBeliefs(S);

    // actions set in the struct fields are not global labels,
    // but are indices with respect to rowgroup of the state
    const std::vector<oneStepBelief>& oneStepBeliefs = computeOneStepBeliefs(*model, observationStates, beliefIndices, stateOneStepBeliefs);
    const auto n_beliefs = oneStepBeliefs.size()+1; // with the initial belief
    printf("N_oneStepBeliefs = %lu\n\n", oneStepBeliefs.size());
    const auto saRowDummy = transitionM.getRow(0);

    /*
    printf("\nOne-step beliefs:\n");
    for (auto& osb : oneStepBeliefs) {
        std::cout << osb << std::endl;
    }
    */

    const auto& numOfActions = getNumOfActionsForObservations(*model);

    // Q-values
    std::vector<std::vector<double>> Q_old(n_beliefs);
    std::vector<std::vector<double>> Q_new(n_beliefs);

    // initialize inner vectors to zero
    // init belief
    Q_old[0] = std::vector<double>(numOfActions[initObs], 0); // 1:1 correspondence with FIB Q-vals
    Q_new[0] = std::vector<double>(numOfActions[initObs], 0);
    // rest
    for (int i = 1; i < n_beliefs; i++) {
        auto len = numOfActions[oneStepBeliefs[i-1].o];
        Q_old[i] = std::vector<double>(len, 0); // future work: initialize with FIB
        Q_new[i] = std::vector<double>(len, 0);
    }

    // limit sets of observations
    std::vector<std::vector<std::unordered_set<uint64_t>>> obsSets(n_beliefs);

    // b0
    for (uint64_t a = 0; a < numOfActions[initObs]; a++) {
        std::unordered_set<uint64_t> obsSet;
        for (const auto& [state, _] : b0) {
            for (const auto& osbIdx : stateOneStepBeliefs[state]) {
                if (const auto& b = oneStepBeliefs[osbIdx]; b.a == a) {
                    obsSet.insert(b.o);
                }
            }
        }

        obsSets[0].push_back(obsSet);
    }

    // rest
    for (int i = 0; i < n_beliefs-1; i++) {
        for (uint64_t a = 0; a < numOfActions[oneStepBeliefs[i].o]; a++) {
            std::unordered_set<uint64_t> obsSet;
            for (auto it = oneStepBeliefs[i].reachableBeliefs.begin(); it != oneStepBeliefs[i].reachableBeliefs.end(); ++it) {
                if (auto& b = oneStepBeliefs[*it]; b.a == a) {
                    obsSet.insert(b.o);
                }
            }

            obsSets[i+1].push_back(obsSet);
        }
    }

    /*
    printf("b0: ");
    for (uint64_t a = 0; a < numOfActions[initObs]; a++) {
        printf("action=%lu: ", a);
        for (auto o : obsSets[0][a]) {
            printf("%lu ", o);
        }
        printf("\n");
    }
    printf("\n");

    for (int i = 1 ; i < n_beliefs; i++) {
        std::cout << "One-step belief: " << oneStepBeliefs[i-1] << std::endl;
        for (uint64_t a = 0; a < numOfActions[oneStepBeliefs[i-1].o]; a++) {
            printf("action=%lu: ", a);
            for (auto o : obsSets[i][a]) {
                printf("%lu ", o);
            }
            printf("\n");
        }
        printf("\n");
    } */

    // Q_TIB updates
    for (int i = 0; i < iterations; i++) {
        printf("##############\niteration: %d\n##############\n\n", i);
        double delta = -std::numeric_limits<double>::infinity();

        // compute new Q-value for initial belief
        for (unsigned long a = 0; a < numOfActions[initObs]; a++) {
            // printf("++++++++++++++++++++++++\nbelief=init\t\taction=%lu\n", a);
            double obsSum = 0;

            // forall observations
            for (const auto& o : obsSets[0][a]) {
                double sum;
                sum = (func == MIN)
                    ? std::numeric_limits<double>::infinity()
                    : -std::numeric_limits<double>::infinity();

                // compute for every a' in A, looking for func
                const auto A = numOfActions[o];
                for (uint64_t aPrime = 0; aPrime < A; aPrime++) {
                    double stateSum = 0;
                    for (const auto& [s,prob] : b0) {
                        for (const auto bIdx : stateOneStepBeliefs[s]) {
                            if (auto& osb = oneStepBeliefs[bIdx]; osb.a == a && osb.o == o) {
                                const auto& saRow = transitionM.getRow(rowGroupIds[s]+a);
                                // std::cout << "debug: accessing value of " << osb << " = " << Q_old[bIdx+1][aPrime] << std::endl;
                                stateSum += prob
                                            * PrOfObs(o, saRow, stateObservations)
                                            * Q_old[bIdx+1][aPrime]; // offset
                            }
                        }
                    }

                    if (func == MIN) {
                        if (stateSum < sum) {
                            sum = stateSum;
                        }
                    } else {
                        if (stateSum > sum) {
                            sum = stateSum;
                        }
                    }
                }

                obsSum += sum;
            }

            double reward = 0;

            for (const auto& [key, val] : b0) {
                const auto choice_id = rowGroupIds[key] + a;
                // printf("debug: choice_id=%lu\n", choice_id);
                double tmpReward = rewardModel.getStateActionReward(choice_id);
                if (stateRewards) {
                    tmpReward += rewardModel.getStateReward(key);
                }

                reward += val * tmpReward;
            }

            // printf("old value: %.2f\t\t", Q_old[0][a]);

            double second_term = discount * obsSum;
            double new_val = reward + second_term;
            Q_new[0][a] = new_val;

            // printf("new value: %.2f + %.2f = %.2f\n", reward, second_term, Q_new[0][a]);

            if (double temp = (new_val - Q_old[0][a])/new_val; temp > delta) {
                delta = temp;
            }
            // printf("++++++++++++++++++++++++\n\n");
        }

        // forall one-step beliefs
        for (auto bIdx = 1; bIdx < n_beliefs; bIdx++) {
            auto& b = oneStepBeliefs[bIdx-1];
            // std::cout << "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\none-step belief: " << b << "\n";

            // forall actions
            for (uint64_t a = 0; a < numOfActions[b.o]; a++) {
                // printf("\naction=%lu:\n", a);
                // Q-TIB
                double obsSum = 0;

                // forall observations
                for (auto o : obsSets[bIdx][a]) {


                    // could be pre-computed
                    std::vector<size_t> relevantOsbIds;

                    // this replaces the loop over all states
                    auto& bv = b.reachableBeliefs;
                    for (auto it = bv.begin(); it != bv.end(); ++it) {
                        if (auto& temp = oneStepBeliefs[*it]; temp.a == a && temp.o == o) {
                            relevantOsbIds.push_back(*it);
                        }
                    }

                    // sum over an empty set is 0
                    if (relevantOsbIds.size() == 0) {
                        // obsSum += 0;
                        continue;
                    }

                    double funcOfStateSum;
                    funcOfStateSum = (func == MIN)
                        ? std::numeric_limits<double>::infinity()
                        : -std::numeric_limits<double>::infinity();

                    const auto A = numOfActions[o];
                    // if there is no future, i.e. A = 0,
                    // we settle for the immediate reward
                    if (A == 0) {
                        continue;
                    }

                    // compute for every a' in A, looking for $func
                    for (uint64_t aPrime = 0; aPrime < A; aPrime++) {
                        double stateSum = 0;
                        for (const auto osbId : relevantOsbIds) {
                            auto& bNext = oneStepBeliefs[osbId];

                            double bOfS;
                            if (PRS_PRECOMPUTED) {
                                // no error handling
                                bOfS = b.stateProbs.at(bNext.s);
                            } else {
                                bOfS = b.bOfS(bNext.s, stateObservations);
                            }

                            // std::cout << "debug: accessing value of " << bNext << " = " << Q_old[osbId+1][aPrime] << std::endl;
                            stateSum += bOfS
                                        * PrOfObs(o, bNext.saRow, stateObservations)
                                        * Q_old[osbId + 1][aPrime];
                        }

                        if (func == MIN) {
                            if (stateSum < funcOfStateSum) {
                                funcOfStateSum = stateSum;
                            }
                        } else {
                            if (stateSum > funcOfStateSum) {
                                funcOfStateSum = stateSum;
                            }
                        }
                    }

                    obsSum += funcOfStateSum;
                }
                // printf("old value: %.2f\t\t", Q_old[bIdx][a]);

                const double reward = beliefActionReward(b, a, rowGroupIds, rewardModel, stateObservations, observationStates[b.o]);
                const double second_term = discount * obsSum;
                double new_val = reward + second_term;
                Q_new[bIdx][a] = new_val;

                // printf("new value: %.2f + %.2f = %.2f\n", reward, second_term, Q_new[bIdx][a]);

                if (double temp = (new_val - Q_old[bIdx][a])/new_val; temp > delta) {
                    delta = temp;
                }
            }

            // printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n\n");
        }

        printf("delta=%.6f\n\n", delta);
        printf("##############\n\n");

        if (discount / (1.0 - discount) * delta < epsilon) {
            printf("precision met, iterations=%d\n", i);
            break;
        }

        Q_old = Q_new;
    }

    double res = Q_new[0][0];
    printf("Q[0] = [");
    for (auto val : Q_new[0]) {
        printf("%.3f, ", val);
        if (val > res) {
            res = val;
        }
    }
    printf("]\n\n");

    return res;
}