#ifndef STORM_PROJECT_STARTER_QFIB_H
#define STORM_PROJECT_STARTER_QFIB_H

#include <storm/api/storm.h>

const std::string MIN = "min";
const std::string MAX = "max";

double Q_FIB(storm::models::sparse::Pomdp<double>& model, const std::string& func, double discount, double initQVal, double epsilon);

#endif //STORM_PROJECT_STARTER_QFIB_H