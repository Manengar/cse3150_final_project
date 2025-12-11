#ifndef BGP_SIMULATOR_V2_H
#define BGP_SIMULATOR_V2_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>

enum class RelationType {
    PROVIDER_TO_CUSTOMER = 0,  // ASN1 is provider of ASN2
    PEER_TO_PEER = 1,          // ASN1 and ASN2 are peers
    CUSTOMER_TO_PROVIDER = 2   // ASN1 is customer of ASN2
};

enum class AnnouncementType {
    LEARNED_FROM_CUSTOMER = 0,
    LEARNED_FROM_PEER = 1,
    LEARNED_FROM_PROVIDER = 2
};

// BGP Route representation
class Route {
public:
    std::string prefix;
    std::vector<int> as_path;
    AnnouncementType announcement_type;
    bool rov_invalid;
    
    Route(const std::string& prefix, const std::vector<int>& as_path, 
          AnnouncementType type, bool rov_invalid = false);
    
    int get_origin_asn() const;
    void prepend(int asn);
    Route copy() const;
};

// AS Graph representation
class ASGraph {
public:
    // adjacency list: asn -> list of (neighbor_asn, relationship_type)
    std::unordered_map<int, std::vector<std::pair<int, RelationType>>> adjacency;
    std::unordered_set<int> all_asns;
    
    void add_relationship(int asn1, int asn2, RelationType rel_type);
    void load_from_file(const std::string& filename);
    std::vector<std::pair<int, RelationType>> get_neighbors(int asn) const;
    void print_stats() const;

    bool has_customer_provider_cycle() const;
};

// BGP Simulator
class BGPSimulator {
private:
    ASGraph& graph;
    std::unordered_set<int> rov_enabled_asns;
    
    // Local RIBs: asn -> prefix -> route
    std::unordered_map<int, std::unordered_map<std::string, std::shared_ptr<Route>>> ribs;
    
    // Message queues for propagation: asn -> prefix -> list of received routes
    std::unordered_map<int, std::unordered_map<std::string, std::vector<std::shared_ptr<Route>>>> message_queues;
    
    // Graph flattening for provider hierarchy
    std::unordered_map<int, int> asn_to_rank;
    std::vector<std::vector<int>> rank_to_asns;
    
    // Helper functions
    void flatten_graph();
    bool better_route(const Route& new_route, const Route& existing_route, int deciding_asn) const;
    bool can_export(const Route& route, RelationType export_relationship) const;
    void send_route_to_neighbor(int sender_asn, int receiver_asn, const Route& route, RelationType relationship);
    void process_messages(int asn);
    AnnouncementType relationship_to_announcement_type(RelationType rel_type) const;
    
public:
    BGPSimulator(ASGraph& graph);
    
    void set_rov_asns(const std::unordered_set<int>& rov_asns);
    void seed_announcement(int origin_asn, const std::string& prefix, bool rov_invalid = false);
    bool propagate();  // Returns false if cycle/infinite loop detected
    void export_ribs_csv(const std::string& filename) const;
    int get_rib_count() const;
};

#endif // BGP_SIMULATOR_V2_H