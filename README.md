## Manasvi Iyengar
## BGP Simulator – CSE 3150 Course Project

This program simulates BGP (Border Gateway Protocol) route propagation over a CAIDA AS-relationship graph, using customer–provider–peer policies and (optional) Route Origin Validation (ROV).
This simulator models realistic BGP behavior using standard customer–provider–peer policies.

1. Topology Loading & Validation
    Parses CAIDA AS relationship data
    Builds a directed AS graph
    Detects invalid customer–provider cycles using DFS
    Exits immediately with a non-zero status if a cycle is found

2. Graph Flattening
    Assigns each AS a hierarchy rank based on customer relationships
    Enables ordered propagation that respects BGP economics

3. Three-Phase BGP Propagation
    UP: customers announce routes to providers
    ACROSS: peers exchange routes
    DOWN: providers announce routes to customers

4. Route Selection
    Routes are selected according to standard BGP rules:
    Customer > Peer > Provider preference
    Shorter AS path preferred
    Deterministic tie-breaking by ASN

5. Convergence & Output
    Propagation repeats until no routing tables change
    Final results are written to ribs.csv in the current directory
    The program must be run on Linux and was created/tested on Ubuntu.

1. Building

From the `src` directory:
    make clean && make

Requirements:
    Linux terminal environment
    g++ with C++17 support
    No external libraries

This produces an executable named:
bgp_simulator

2. Running the Simulator

    Prefix test:
    ./bgp_simulator --relationships
  ../bench/prefix/CAIDAASGraphCollector_2025.10.16.txt --announcements
  ../bench/prefix/anns.csv --rov-asns ../bench/prefix/rov_asns.csv
  ../bench/compare_output.sh ../bench/prefix/ribs.csv ribs.csv

    Subprefix test:
     ./bgp_simulator --relationships
  ../bench/subprefix/CAIDAASGraphCollector_2025.10.16.txt
  --announcements ../bench/subprefix/anns.csv --rov-asns
  ../bench/subprefix/rov_asns.csv
  ../bench/compare_output.sh ../bench/subprefix/ribs.csv ribs.csv
 
    Many test:
    ./bgp_simulator --relationships
  ../bench/many/CAIDAASGraphCollector_2025.10.15.txt --announcements
  ../bench/many/anns.csv --rov-asns ../bench/many/rov_asns.csv
  ../bench/compare_output.sh ../bench/many/ribs.csv ribs.csv

## ALL TESTS PASS and outputs ✓ Files match perfectly!

Cycle Check:
When loading the CAIDA file:

    1. It builds a directed graph where edges represent customer → provider
    
    2. Performs a DFS with recursion-stack tracking
    
    3. If a cycle is detected:
        a. Prints an error message
        b. Exits immediately
        c. Returns exit code 1
        d. Does not run propagation
        e. Does not write ribs.csv

Time for many test to finish running:
    real    0m31.925s
    user    0m30.576s
    sys     0m1.187s
