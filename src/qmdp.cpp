#include "qmdp.h"
#include <storm/api/storm.h>

double Q_MDP(const std::shared_ptr<storm::models::sparse::Pomdp<double>>& model, std::string func, double discount, double initQVal, double epsilon) {
    assert(model->getNumberOfRewardModels() == 1);
    assert(func == MIN || func == MAX);

    const auto C = model->getNumberOfChoices();

    auto& transitionM = model->getTransitionMatrix();
    auto& rewardModel = model->getRewardModels().begin()->second;
    std::vector<double> Q_old(C, initQVal);
    std::vector<double> Q_new(C);

    auto& rgids = transitionM.getRowGroupIndices();
    std::unordered_set<unsigned long int> sinkChoices;
    const auto rgc = transitionM.getRowGroupCount();
    for (uint64_t i = 0; i < rgc; i++) {
        if (model->isSinkState(i)) {
            std::printf("sink state: %lu --> ", i);
            for (auto j = rgids[i]; j < rgids[i+1]; j++) {
                sinkChoices.insert(j);
                std::printf("row %lu: [", j);
                for (auto row = transitionM.getRow(j); auto& e : row) {
                    std::printf("to-state=%lu prob=%.2f,", e.getColumn(), e.getValue());
                }
                std::printf("]\n");
            }
        }
    }
    std::cout << std::endl;

    auto it = 0;
    while (true) {
        double delta = 0;

        // t+1 update, iterate over all state-action pairs
        for (uint_fast64_t i = 0; i < C; i++) {
            if (sinkChoices.contains(i)) {
                Q_new[i] = 0;
                continue;
            }
            double sum = 0;
            // non-zero probabilities, meaningful to the sum,
            // are exactly the entries in a row with i=current choice,
            // since the action stays the same. iterate over the row:
            for (auto row = transitionM.getRow(i); auto& e : row) {
                double cmp;
                if (func == MIN) {
                    cmp = std::numeric_limits<double>::infinity();
                } else {
                    cmp = -std::numeric_limits<double>::infinity();
                }
                auto col = e.getColumn(); // b_s' = column; state the transition is being made to

                // corresponds to where choices for s' begin and end
                for (uint_fast64_t j = rgids[col]; j < rgids[col+1]; j++) {
                    if (func == MIN) {
                        if (Q_old[j] < cmp) {
                            cmp = Q_old[j];
                        }
                    } else {
                        if (Q_old[j] > cmp) {
                            cmp = Q_old[j];
                        }
                    }
                }

                // value = probability
                sum += e.getValue() * cmp;
            }

            Q_new[i] = rewardModel.getStateActionReward(i) + discount*sum;

            delta = std::max(delta, std::abs(Q_new[i] - Q_old[i]));
        }
        it++;
        std::cout << "iteration: " << it << "\tdelta=" << delta << std::endl;

        if (delta < epsilon) {
            break;
        }

        Q_old = Q_new;
    }

    auto upperBound = -std::numeric_limits<double>::infinity();
    for (const auto bit : model->getInitialStates()) {
        // for each initial state, check Q-values
        // of every possible choice from it
        for (auto i = rgids[bit]; i < rgids[bit+1]; i++) {
            // look for the upper bound
            if (Q_new[i] > upperBound) {
                upperBound = Q_new[i];
            }
        }
    }

    std::cout << std::endl;
    for (int i = 0; i < C; i++) {
        std::cout << "Q[" << i << "] = " << Q_new[i] << "\t";
    }
    std::cout << std::endl;

    return upperBound;
}