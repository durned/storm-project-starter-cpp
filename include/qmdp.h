#ifndef STORM_PROJECT_STARTER_QMDP_H
#define STORM_PROJECT_STARTER_QMDP_H

#include <storm/api/storm.h>

const std::string MIN = "min";
const std::string MAX = "max";

double Q_MDP(const std::shared_ptr<storm::models::sparse::Pomdp<double>>& model, std::string func, double discount, double initQVal, double epsilon);

#endif //STORM_PROJECT_STARTER_QMDP_H