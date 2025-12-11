#include "bgp_simulator.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <getopt.h>
#include <cstring>

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [OPTIONS]\n"
              << "\nRequired Options:\n"
              << "  --relationships FILE   Path to AS relationships file (CAIDA format)\n"
              << "  --announcements FILE   Path to announcements CSV file\n"
              << "\nOptional Options:\n"
              << "  --rov-asns FILE        Path to ROV-enabled ASNs file\n"
              << "  --help                 Show this help message\n"
              << "\nOutput:\n"
              << "  Creates ribs.csv in the current directory\n"
              << "\nExample:\n"
              << "  " << program_name << " --relationships topology.txt \\\n"
              << "                       --announcements anns.csv \\\n"
              << "                       --rov-asns rov_asns.csv\n";
}

std::unordered_set<int> load_rov_asns(const std::string& filename) {
    std::unordered_set<int> rov_asns;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open ROV ASNs file: " << filename << std::endl;
        return rov_asns;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Remove whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        try {
            int asn = std::stoi(line);
            rov_asns.insert(asn);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Skipping invalid ASN: " << line << std::endl;
        }
    }
    
    return rov_asns;
}

void load_announcements(BGPSimulator& sim, const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open announcements file: " + filename);
    }
    
    std::string line;
    // Skip header
    std::getline(file, line);
    
    int count = 0;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string seed_asn_str, prefix, rov_invalid_str;
        
        if (std::getline(iss, seed_asn_str, ',') &&
            std::getline(iss, prefix, ',') &&
            std::getline(iss, rov_invalid_str)) {
            
            int seed_asn = std::stoi(seed_asn_str);
            bool rov_invalid = (rov_invalid_str.find("True") != std::string::npos ||
                               rov_invalid_str.find("true") != std::string::npos ||
                               rov_invalid_str.find("1") != std::string::npos);
            
            sim.seed_announcement(seed_asn, prefix, rov_invalid);
            count++;
        }
    }
    
    std::cout << "Loaded " << count << " announcements\n";
}

int main(int argc, char* argv[]) {
    std::string relationships_file;
    std::string announcements_file;
    std::string rov_asns_file;
    
    // Define long options
    static struct option long_options[] = {
        {"relationships", required_argument, 0, 'r'},
        {"announcements", required_argument, 0, 'a'},
        {"rov-asns",      required_argument, 0, 'v'},
        {"help",          no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    
    // Parse command line arguments
    while ((opt = getopt_long(argc, argv, "r:a:v:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'r':
                relationships_file = optarg;
                break;
            case 'a':
                announcements_file = optarg;
                break;
            case 'v':
                rov_asns_file = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Validate required arguments
    if (relationships_file.empty() || announcements_file.empty()) {
        std::cerr << "Error: --relationships and --announcements are required\n\n";
        print_usage(argv[0]);
        return 1;
    }
    
    try {
        std::cout << "==========================================\n";
        std::cout << "BGP Simulator V2\n";
        std::cout << "==========================================\n\n";
        
        // Load AS graph
        std::cout << "Loading AS relationships from " << relationships_file << "...\n";
        ASGraph graph;
        graph.load_from_file(relationships_file);
        graph.print_stats();
        std::cout << "\n";

        // customer–provider cycle detection
        if (graph.has_customer_provider_cycle()) {
            std::cerr << "Error: customer-provider cycle detected in AS relationships\n";
            return 1;  // non-zero exit code as your friend described
        }
        
        // Create simulator
        BGPSimulator sim(graph);
        
        // Load ROV ASNs if provided
        if (!rov_asns_file.empty()) {
            std::cout << "Loading ROV ASNs from " << rov_asns_file << "...\n";
            auto rov_asns = load_rov_asns(rov_asns_file);
            sim.set_rov_asns(rov_asns);
            std::cout << "Loaded " << rov_asns.size() << " ROV-enabled ASes\n\n";
        }
        
        // Load and seed announcements
        std::cout << "Loading announcements from " << announcements_file << "...\n";
        load_announcements(sim, announcements_file);
        std::cout << "\n";
        
        // Run propagation
        bool converged = sim.propagate();
        std::cout << "\n";
        
        if (!converged) {
            std::cerr << "BGP propagation failed due to routing cycles!\n";
            return 1;  // Non-zero exit code for cycle detection
        }
        
        // Export RIBs to ribs.csv in current directory
        std::string output_file = "ribs.csv";
        std::cout << "Exporting RIBs to " << output_file << "...\n";
        sim.export_ribs_csv(output_file);
        
        std::cout << "Total RIB entries: " << sim.get_rib_count() << "\n";
        std::cout << "\n==========================================\n";
        std::cout << "Complete! Output written to ribs.csv\n";
        std::cout << "==========================================\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}