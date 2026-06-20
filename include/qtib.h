#ifndef STORM_PROJECT_STARTER_QTIB_H
#define STORM_PROJECT_STARTER_QTIB_H

#include <storm/api/storm.h>

const std::string MIN = "min";
const std::string MAX = "max";

typedef std::unordered_map<uint64_t, double> probDist;

struct CLIArgsQTIB {
    std::string input;
    std::string constDefs = "";
    std::string formula;
    std::string func = MAX;
    int h               = 250;
    double gamma        = 0.95;
    double epsilon      = 1e-3;
};

struct oneStepBelief {
    uint64_t s, a, o;
    storm::storage::SparseMatrix<double>::const_rows saRow;
    storm::storage::BitVector reachableBeliefs = storm::storage::BitVector();
    probDist stateProbs;

    bool operator==(const oneStepBelief& other) const {
        return s == other.s && a == other.a && o == other.o;
    }

    [[nodiscard]]
    double bOfS(const uint64_t sPrime, const std::vector<uint32_t>& stateObs) const {
        if (stateObs[sPrime] != o) {
            return 0;
        }

        double numerator = 0;
        double denominator = 0;
        for (const auto& entry : saRow) {
            if (entry.getColumn() == sPrime) {
                numerator = entry.getValue();
            }

            if (stateObs[entry.getColumn()] == o) {
                denominator += entry.getValue();
            }
        }

        return numerator/denominator;
    }
};

inline std::ostream& operator<<(std::ostream& os, const oneStepBelief& osb) {
    return os << "b_{s=" << osb.s << ", a=" << osb.a << ", o=" << osb.o << "}\t" << osb.reachableBeliefs;
}

struct oneStepBeliefHash {
    size_t operator()(const oneStepBelief& belief) const {
        return std::hash<uint64_t>()(belief.s) ^
               (std::hash<uint64_t>()(belief.a) << 1) ^
               (std::hash<uint64_t>()(belief.o) << 2);
    }
};

double Q_TIB(storm::models::sparse::Pomdp<double>& model, const std::string& func, int iterations, double discount, double epsilon);

#endif //STORM_PROJECT_STARTER_QTIB_H