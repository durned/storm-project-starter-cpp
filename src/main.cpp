#include <cstdio>
#include <format>
#include <filesystem>

#include "qtib.h"

#include <storm/api/storm.h>
#include <storm/utility/initialize.h>
#include <storm-parsers/api/storm-parsers.h>
#include <storm-parsers/parser/DirectEncodingParser.h>
#include <storm/transformer/MakePOMDPCanonic.h>
#include <storm/environment/solver/MinMaxSolverEnvironment.h>
#include <storm-pomdp/modelchecker/BeliefExplorationPomdpModelChecker.h>
#include <storm-pomdp/modelchecker/BeliefExplorationPomdpModelCheckerOptions.h>


typedef storm::models::sparse::Pomdp<double> Pomdp;
typedef storm::pomdp::modelchecker::BeliefExplorationPomdpModelChecker<Pomdp> PomdpModelChecker;

void run(CLIArgsQTIB& args) {
    FILE* log = fopen(std::format("tib_{}.log", args.input.stem().string()).c_str(), "w");

    std::string formulaAsString = args.formula;
    std::shared_ptr< storm::logic::Formula const > formula;

    if (args.inModelFmt == InputModelFmt::Prism) {
        auto program = storm::api::parseProgram(args.input.string());
        assert(program.getModelType() == storm::prism::Program::ModelType::POMDP);
        program = storm::utility::prism::preprocess(program, args.constDefs);

        formula = storm::api::parsePropertiesForPrismProgram(formulaAsString, program).front().getRawFormula();
        /* auto options = storm::builder::BuilderOptions(true, true);
        options.setBuildStateValuations(true);
        options.setBuildChoiceLabels(true); */
        auto model = storm::api::buildSparseModel<double>(program, {formula})->as<Pomdp>();

        storm::transformer::MakePOMDPCanonic<double> makeCanonic(*model);
        model = makeCanonic.transform();
        assert(model->isCanonic());

        const auto myResult = Q_TIB(model, args.func, args.precompute, args.h, args.gamma, args.epsilon, log, args.timeout);
        // printf("Q_TIB finished: result=%.2f\n", myResult);
    } else {
        auto options = storm::parser::DirectEncodingParserOptions();
        options.buildChoiceLabeling = true;

        auto model = storm::parser::parseDirectEncodingModel<double>(args.input, options)->as<Pomdp>();
        assert(model->getType() == storm::models::ModelType::Pomdp);

        storm::transformer::MakePOMDPCanonic<double> makeCanonic(*model);
        model = makeCanonic.transform();
        assert(model->isCanonic());

        const auto myResult = Q_TIB(model, args.func, args.precompute, args.h, args.gamma, args.epsilon, log, args.timeout);
        // printf("Q_TIB finished: result=%.2f\n", myResult);
    }

    /* built-in checker
    storm::pomdp::modelchecker::BeliefExplorationPomdpModelCheckerOptions<double> opt(true, true);  // Always compute both bounds (lower and upper)
    opt.gapThresholdInit = 0;

    PomdpModelChecker checker(model, opt);

    storm::Environment env;
    env.solver().minMax().setMethod(storm::solver::MinMaxMethod::ValueIteration);
    env.solver().minMax().setPrecision(storm::utility::convertNumber<storm::RationalNumber>(1e-3));

    const auto checkerResult = checker.check(env, *formula);
    printf("checker finished: result_lower=%.2f\tresult_upper=%.2f\n\n", checkerResult.lowerBound, checkerResult.upperBound);
    */

    fclose(log);
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
        std::cerr << "provide at least the model file in DRN or PRISM fmt, if required the constant definitions, and the formula." << std::endl;
    } else {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            const bool cond = i + 1 < argc;

            if (arg == "--input" && cond) {
                std::filesystem::path p(argv[++i]);
                args.input = p;

                if (p.extension() == ".drn") {
                    args.inModelFmt = InputModelFmt::Drn;
                } else if (p.extension() == ".nm" || p.extension() == ".prism") {
                    args.inModelFmt = InputModelFmt::Prism;
                } else {
                    std::cerr << "unsupported input fmt: " << arg << ". must be either \".drn\", \".nm\", or \".prism\"" << std::endl;
                    exit(1);
                }
            } else if (arg == "--constdefs" && cond) {
                args.constDefs = argv[++i];
            } else if (arg == "--formula" && cond) {
                args.formula = argv[++i];
            } else if (arg == "--func" && cond) {
                args.func = argv[++i];
            } else if (arg == "--precompute") {
                args.precompute = true;
            } else if (arg == "-h" && cond) {
                args.h = std::stoi(argv[++i]);
            } else if (arg == "--gamma" && cond) {
                args.gamma = std::stod(argv[++i]);
            } else if (arg == "--epsilon" && cond) {
                args.epsilon = std::stod(argv[++i]);
            } else if (arg == "--timeout" && cond) {
                args.timeout = std::stoi(argv[++i]);
            } else {
                std::cerr << "unknown or incomplete arg: " << arg << std::endl;
                exit(1);
            }
        }
    }

    printf("fmt: %s, constdefs: \"%s\"\n", args.inModelFmt == InputModelFmt::Prism ? "prism" : "drn", args.constDefs);

    run(args);
}
