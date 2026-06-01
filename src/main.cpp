#include <format>

#include "qtib.h"

#include <storm/api/storm.h>
#include <storm/utility/initialize.h>
#include <storm-parsers/api/storm-parsers.h>
#include <storm-pomdp/modelchecker/BeliefExplorationPomdpModelChecker.h>
#include <storm-pomdp/modelchecker/BeliefExplorationPomdpModelCheckerOptions.h>
#include <storm/environment/solver/MinMaxSolverEnvironment.h>
#include <storm/transformer/MakePOMDPCanonic.h>

typedef storm::models::sparse::Pomdp<double> Pomdp;
typedef storm::pomdp::modelchecker::BeliefExplorationPomdpModelChecker<Pomdp> PomdpModelChecker;

void run(CLIArgsQTIB args) {
    // Assumes that the model is in the prism program language format and parses the program.
    auto program = storm::api::parseProgram(args.input);
    assert(program.getModelType() == storm::prism::Program::ModelType::POMDP);
    program = storm::utility::prism::preprocess(program, args.constDefs);

    std::string formulaAsString = "P" + args.func + "=? [F \"goal\"]";
    auto formula = storm::api::parsePropertiesForPrismProgram(formulaAsString, program).front().getRawFormula();

    /* auto options = storm::builder::BuilderOptions(true, true);
    options.setBuildStateValuations(true);
    options.setBuildChoiceLabels(true); */

    auto model = storm::api::buildSparseModel<double>(program, {formula})->as<Pomdp>();
    storm::transformer::MakePOMDPCanonic<double> makeCanonic(*model);
    model = makeCanonic.transform();
    assert(model->isCanonic());

    storm::pomdp::modelchecker::BeliefExplorationPomdpModelCheckerOptions<double> opt(true, true);  // Always compute both bounds (lower and upper)
    opt.gapThresholdInit = 0;

    PomdpModelChecker checker(model, opt);

    storm::Environment env;
    env.solver().minMax().setMethod(storm::solver::MinMaxMethod::ValueIteration);
    env.solver().minMax().setPrecision(storm::utility::convertNumber<storm::RationalNumber>(1e-3));

    const auto checkerResult = checker.check(env, *formula);
    printf("checker finished: result_lower=%.2f\tresult_upper=%.2f\n\n", checkerResult.lowerBound, checkerResult.upperBound);

    const auto myResult = Q_TIB(*model, args.func, args.h, args.gamma, args.epsilon);
    printf("Q_TIB finished: result=%.2f\n", myResult);
}

int main(int argc, char* argv[]) {
    // std::cout << std::filesystem::current_path() << std::endl;
    // std::cout << __cplusplus << std::endl;

    // Init loggers
    storm::utility::setUp();
    // Set some settings objects.
    storm::settings::initializeAll("storm-starter-project", "storm-starter-project");

    // Parse input
    CLIArgsQTIB args;

    if (argc == 1) {
        args.input = "../../examples/simple.prism";
        args.constDefs = "slippery=0";
    } else {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "--input" && i + 1 < argc) {
                args.input = argv[++i];
            } else if (arg == "--constdefs" && i + 1 < argc) {
                args.constDefs = argv[++i];
            } else if (arg == "--func" && i + 1 < argc) {
                args.func = argv[++i];
            } else if (arg == "-h" && i + 1 < argc) {
                args.h = std::stoi(argv[++i]);
            } else if (arg == "--gamma" && i + 1 < argc) {
                args.gamma = std::stod(argv[++i]);
            } else if (arg == "--epsilon" && i + 1 < argc) {
                args.epsilon = std::stod(argv[++i]);
            } else {
                std::cerr << "unknown or incomplete arg: " << arg << std::endl;
                exit(1);
            }
        }
    }

    std::cout << args.input << "\n";
    std::cout << args.constDefs << "\n";
    std::cout << args.func << "\n";
    std::cout << args.h << "\n";
    std::cout << args.gamma << "\n";
    std::cout << args.epsilon << "\n";

    run(args);
}
