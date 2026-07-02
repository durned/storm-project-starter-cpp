# TIB in Storm

## Getting Started
Before starting, make sure that you have some form of Storm installed. We use a [Docker container](https://www.stormchecker.org/documentation/obtain-storm/docker.html) which comes with required dependencies installed.

The following instructions are specific to the Docker setup. Clone the repository and make sure you are in the same directory as the Dockerfile. 
Build the docker container (will take some time to fetch the image):
```
docker build -t <container name> .
```
Start it and navigate to the directory of the built executable:
```
docker run -it <container name> bash
cd /opt/storm-project-starter-cpp/build
```

The executable requires a relative path to the input model in the **PRISM** language which you would like to run TIB on.  
You must also specify a reward formula so that Storm can build it properly. Depending on the model, you may want to set the goal (`min` or `max` reward respectively), as well as some constants. 

An example input using `grid.prism` (the repository comes with it) would look like so:
```
./starter-project --input ../../examples/eval_any/grid.prism --constdefs N=4 --formula "Rmin=? [F target ]" --func min
```

The output should contain some logs and, with any luck, end with:
```
Q_TIB finished: result=2.91
WARN  (BeliefMdpExplorer.cpp:924): Computed values are smaller than the lower bound.
checker finished: result_lower=3.38	result_upper=3.88
```

Just in case, consult this `struct` in case of misalignments in the README and how your input is handled:
```
struct CLIArgsQTIB {
    std::string input;
    std::string constDefs = "";
    std::string formula;
    std::string func = MAX;
    int h               = 250;
    double gamma        = 0.95;
    double epsilon      = 1e-3;
};
```
..and `main.cpp` for parsing logic. 

### More Arguments
  - `int -h`: max number of iterations, default is 250.
  - `double --gamma`: discount factor, default is 0.95.
  - `double --epsilon`: satisfactory precision for early termination, default is 1e-3.

## Current Shortcomings
  - Initial values are all zeros. A possibility of future work is to use FIB to initialize.
  - Some rather trivial, repetitive computations are not yet pre-computed.
  <!-- - Non-canonical models are not (yet?) supported. -->
