#include "qfib.h"
#include <storm/api/storm.h>

typedef std::unordered_map<unsigned long, double> MyMap;
typedef storm::models::sparse::Pomdp<double> Pomdp;

// when no actions for a state are defined (sink),
// there still exists a self-loop entry in the
// transition matrix; this function finds those
// state-action pairs (rows), which will be skipped
std::unordered_set<uint64_t>& findSinkChoices(Pomdp& model) {
    const auto& transitionM = model.getTransitionMatrix();
    const auto rowGroupCount = transitionM.getRowGroupCount();

    static std::unordered_set<uint64_t> sinkChoices;
    auto& rowGroupIds = transitionM.getRowGroupIndices();

    for (auto rowGroup = 0; rowGroup < rowGroupCount; rowGroup++) {
        if (model.isSinkState(rowGroup)) {
            uint64_t until;
            if (rowGroup+1 == rowGroupCount) { // out-of-bounds check
                until = rowGroupIds[rowGroup] + transitionM.getRowGroupEntryCount(rowGroup);
            } else {
                until = rowGroupIds[rowGroup+1];
            }

            for (auto i = rowGroupIds[rowGroup]; i < until; i++) {
                sinkChoices.insert(i);
            }
        }
    }

    return sinkChoices;
}

uint_fast64_t getStateOfChoice(const uint_fast64_t choice, const std::vector<uint64_t>& rowGroupIds, const uint64_t rowCount) {
    const auto size = rowGroupIds.size();
    for (auto i = 0; i < size; i++) {
        if (i+1 == size) {
            if (choice >= rowGroupIds[i] && choice < rowCount) {
                return i;
            }
            break;
        }

        if (choice >= rowGroupIds[i] && choice < rowGroupIds[i+1]) {
            return i;
        }
    }

    return -1;
}

double getUpperBound(const storm::storage::BitVector& initStates, const std::vector<double>& Q_new, const std::vector<uint64_t>& rowGroupIds) {
    auto upperBound = -std::numeric_limits<double>::infinity();
    for (const auto bit : initStates) {
        // for each initial state, check Q-values
        // of every possible choice from it
        for (auto i = rowGroupIds[bit]; i < rowGroupIds[bit+1]; i++) {
            // look for the upper bound
            if (Q_new[i] > upperBound) {
                upperBound = Q_new[i];
            }
        }
    }

    return upperBound;
}

double Q_FIB(storm::models::sparse::Pomdp<double>& model, const std::string& func, const double discount, const double initQVal, const double epsilon) {
    assert(func == MIN || func == MAX);
    assert(model.getNumberOfRewardModels() == 1);

    const auto C = model.getNumberOfChoices();
    const auto& rewardModel = model.getRewardModels().begin()->second;
    const auto sinkChoices = findSinkChoices(model);

    auto& transitionM = model.getTransitionMatrix();
    auto& rowGroupIds = transitionM.getRowGroupIndices();
    auto& stateObservations = model.getObservations();
    std::vector<double> Q_old(C, initQVal);
    std::vector<double> Q_new(C);

    int it = 0;
    while (true) {
        double delta = 0;

        // t+1 update, iterate over all state-action pairs
        for (uint_fast64_t choice = 0; choice < C; choice++) {
            if (sinkChoices.contains(choice)) {
                Q_new[choice] = 0;
                continue;
            }

            double obsSum = 0;
            for (uint64_t o = 0; o < model.getNrObservations(); o++) {
                MyMap sumParts;

                // iterate over the outcomes of (b,a)
                for (auto row = transitionM.getRow(choice); auto& cell : row) {
                    auto sPrime = cell.getColumn();

                    double obsPr;
                    if (o == stateObservations[sPrime]) {
                        obsPr = 1;
                    } else {
                        obsPr = 0;
                    }

                    if (double Pr = cell.getValue() * obsPr; Pr != 0) {
                        sumParts.emplace(sPrime, Pr);
                    }
                }

                if (sumParts.empty()) {
                    continue;
                }

                // Assumptions:
                // - States with the same observation have exactly the same actions available (enforced by makeCanonic)
                // - The same set of actions (or generally all actions) come in the same order in every rowGroup (can be enforced by comparing against labels)
                const auto memberState = sumParts.begin()->first;
                double cmp;
                if (func == MIN) {
                    cmp = std::numeric_limits<double>::infinity();
                } else {
                    cmp = -std::numeric_limits<double>::infinity();
                }

                for (uint64_t action = 0; action < transitionM.getRowGroupEntryCount(memberState); action++) {
                    double innerSum = 0;
                    for (auto [sPrime, Pr] : sumParts) {
                        innerSum += Pr * Q_old[rowGroupIds[sPrime]+action];
                    }

                    if (func == MIN) {
                        if (innerSum < cmp) {
                            cmp = innerSum;
                        }
                    } else {
                        if (innerSum > cmp) {
                            cmp = innerSum;
                        }
                    }
                }

                obsSum += cmp;
            }

            const uint_fast64_t state = getStateOfChoice(choice, rowGroupIds, transitionM.getRowCount());
            // std::printf("choice=%lu\tstate=%lu\nstateR=%.2f\tactionR=%.2f\n", choice, state, rewardModel.getStateReward(state), rewardModel.getStateActionReward(choice));

            const double immediateReward = rewardModel.getStateReward(state) + rewardModel.getStateActionReward(choice);
            Q_new[choice] = immediateReward + discount * obsSum;

            delta = std::max(delta, std::abs(Q_new[choice] - Q_old[choice]));
        }
        it++;
        std::printf("iteration: %d\tdelta=%.2f\n", it, delta);

        if (delta < epsilon) {
            break;
        }

        Q_old = Q_new;
    }
    std::cout << std::endl;

    for (int i = 0; i < C; i++) {
        std::printf("Q[%d]=%.2f\t", i, Q_new[i]);
    }
    std::cout << std::endl;

    return getUpperBound(model.getInitialStates(), Q_new, rowGroupIds);
}