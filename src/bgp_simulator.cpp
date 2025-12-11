#include "bgp_simulator.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <queue>
#include <functional>

// Route Implementation
Route::Route(const std::string& prefix, const std::vector<int>& as_path, 
             AnnouncementType type, bool rov_invalid)
    : prefix(prefix), as_path(as_path), announcement_type(type), rov_invalid(rov_invalid) {}

int Route::get_origin_asn() const {
    return as_path.empty() ? -1 : as_path.back();
}

void Route::prepend(int asn) {
    as_path.insert(as_path.begin(), asn);
}

Route Route::copy() const {
    return Route(prefix, as_path, announcement_type, rov_invalid);
}

// ASGraph Implementation
void ASGraph::add_relationship(int asn1, int asn2, RelationType rel_type) {
    adjacency[asn1].push_back({asn2, rel_type});
    
    RelationType reverse_rel = RelationType::PEER_TO_PEER;
    switch (rel_type) {
        case RelationType::PROVIDER_TO_CUSTOMER:
            reverse_rel = RelationType::CUSTOMER_TO_PROVIDER;
            break;
        case RelationType::CUSTOMER_TO_PROVIDER:
            reverse_rel = RelationType::PROVIDER_TO_CUSTOMER;
            break;
        case RelationType::PEER_TO_PEER:
            reverse_rel = RelationType::PEER_TO_PEER;
            break;
    }
    adjacency[asn2].push_back({asn1, reverse_rel});
    
    all_asns.insert(asn1);
    all_asns.insert(asn2);
}

void ASGraph::load_from_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }

    std::string line;
    int relationships_loaded = 0;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        // CAIDA-style: fields separated by '|'
        std::replace(line.begin(), line.end(), '|', ' ');

        std::istringstream iss(line);
        int asn1, asn2, rel_type_int;
        std::string bgp_label;  // trailing field, can ignore

        if (!(iss >> asn1 >> asn2 >> rel_type_int)) {
            continue;
        }
        // If there is a fourth field, eat it so we don't care
        iss >> bgp_label;

        RelationType rel_type;
        if (rel_type_int == -1) {
            // asn1 is PROVIDER of asn2
            rel_type = RelationType::PROVIDER_TO_CUSTOMER;
        } else if (rel_type_int == 0) {
            // peers
            rel_type = RelationType::PEER_TO_PEER;
        } else {
            // ignore special/unknown rels
            continue;
        }

        add_relationship(asn1, asn2, rel_type);
        relationships_loaded++;
    }

    std::cout << "Loaded " << relationships_loaded
              << " relationships for " << all_asns.size() << " ASNs\n";
}

std::vector<std::pair<int, RelationType>> ASGraph::get_neighbors(int asn) const {
    auto it = adjacency.find(asn);
    if (it != adjacency.end()) {
        return it->second;
    }
    return {};
}

void ASGraph::print_stats() const {
    int customer_relationships = 0, peer_relationships = 0, provider_relationships = 0;
    
    for (const auto& asn_entry : adjacency) {
        for (const auto& neighbor : asn_entry.second) {
            switch (neighbor.second) {
                case RelationType::CUSTOMER_TO_PROVIDER:
                    customer_relationships++;
                    break;
                case RelationType::PEER_TO_PEER:
                    peer_relationships++;
                    break;
                case RelationType::PROVIDER_TO_CUSTOMER:
                    provider_relationships++;
                    break;
            }
        }
    }
    
    std::cout << "Graph stats - ASNs: " << all_asns.size() 
              << ", Customer rels: " << customer_relationships 
              << ", Peer rels: " << peer_relationships 
              << ", Provider rels: " << provider_relationships << "\n";
}

bool ASGraph::has_customer_provider_cycle() const {
    enum class Color { WHITE, GRAY, BLACK };

    // Initialize all nodes as unvisited (WHITE)
    std::unordered_map<int, Color> color;
    for (int asn : all_asns) {
        color[asn] = Color::WHITE;
    }

    // DFS following ONLY customer -> provider edges
    std::function<bool(int)> dfs = [&](int u) -> bool {
        color[u] = Color::GRAY;

        auto it = adjacency.find(u);
        if (it != adjacency.end()) {
            for (const auto& neighbor : it->second) {
                int v = neighbor.first;
                RelationType rel = neighbor.second;

                // We want directed edges: u --CUSTOMER_TO_PROVIDER--> v
                if (rel != RelationType::CUSTOMER_TO_PROVIDER) {
                    continue;
                }

                if (color[v] == Color::GRAY) {
                    // Back edge → cycle
                    return true;
                }
                if (color[v] == Color::WHITE && dfs(v)) {
                    return true;
                }
            }
        }

        color[u] = Color::BLACK;
        return false;
    };

    // Run DFS from every unvisited node
    for (int asn : all_asns) {
        if (color[asn] == Color::WHITE) {
            if (dfs(asn)) {
                return true;
            }
        }
    }

    return false;
}

// BGPSimulator Implementation
BGPSimulator::BGPSimulator(ASGraph& graph) : graph(graph) {}

void BGPSimulator::set_rov_asns(const std::unordered_set<int>& rov_asns) {
    rov_enabled_asns = rov_asns;
}

void BGPSimulator::seed_announcement(int origin_asn, const std::string& prefix, bool rov_invalid) {
    graph.all_asns.insert(origin_asn);
    
    auto route = std::make_shared<Route>(prefix, std::vector<int>{origin_asn}, 
                                        AnnouncementType::LEARNED_FROM_CUSTOMER, rov_invalid);
    ribs[origin_asn][prefix] = route;
    
    std::cout << "Seeded: AS " << origin_asn << " -> " << prefix;
    if (rov_invalid) std::cout << " (ROV INVALID)";
    std::cout << "\n";
}

void BGPSimulator::flatten_graph() {
    asn_to_rank.clear();
    rank_to_asns.clear();
    
    std::cout << "Flattening graph with " << graph.all_asns.size() << " ASNs...\n";
    
    std::unordered_map<int, int> customer_count;
    for (int asn : graph.all_asns) {
        customer_count[asn] = 0;
        for (const auto& neighbor : graph.get_neighbors(asn)) {
            if (neighbor.second == RelationType::PROVIDER_TO_CUSTOMER) {
                customer_count[asn]++;
            }
        }
    }
    
    std::queue<int> zero_customer_queue;
    for (const auto& entry : customer_count) {
        if (entry.second == 0) {
            zero_customer_queue.push(entry.first);
        }
    }
    
    int current_rank = 0;
    while (!zero_customer_queue.empty()) {
        int level_size = zero_customer_queue.size();
        rank_to_asns.push_back(std::vector<int>());
        
        for (int i = 0; i < level_size; i++) {
            int asn = zero_customer_queue.front();
            zero_customer_queue.pop();
            
            asn_to_rank[asn] = current_rank;
            rank_to_asns[current_rank].push_back(asn);
            
            for (const auto& neighbor : graph.get_neighbors(asn)) {
                if (neighbor.second == RelationType::CUSTOMER_TO_PROVIDER) {
                    customer_count[neighbor.first]--;
                    if (customer_count[neighbor.first] == 0) {
                        zero_customer_queue.push(neighbor.first);
                    }
                }
            }
        }
        current_rank++;
    }
    
    std::cout << "Found " << rank_to_asns[0].size() << " rank-0 ASNs\n";
    std::cout << "Graph flattened into " << rank_to_asns.size() << " ranks\n";
    for (size_t i = 0; i < rank_to_asns.size(); i++) {
        std::cout << "  Rank " << i << ": " << rank_to_asns[i].size() << " ASNs\n";
    }
}

AnnouncementType BGPSimulator::relationship_to_announcement_type(RelationType rel_type) const {
    switch (rel_type) {
        case RelationType::CUSTOMER_TO_PROVIDER:
            return AnnouncementType::LEARNED_FROM_CUSTOMER;
        case RelationType::PEER_TO_PEER:
            return AnnouncementType::LEARNED_FROM_PEER;
        case RelationType::PROVIDER_TO_CUSTOMER:
            return AnnouncementType::LEARNED_FROM_PROVIDER;
    }
    return AnnouncementType::LEARNED_FROM_PROVIDER;
}

bool BGPSimulator::can_export(const Route& route, RelationType export_relationship) const {
    switch (route.announcement_type) {
        case AnnouncementType::LEARNED_FROM_CUSTOMER:
            return true;
            
        case AnnouncementType::LEARNED_FROM_PROVIDER:
            return export_relationship == RelationType::PROVIDER_TO_CUSTOMER;
            
        case AnnouncementType::LEARNED_FROM_PEER:
            return export_relationship == RelationType::PROVIDER_TO_CUSTOMER;
    }
    return false;
}

bool BGPSimulator::better_route(const Route& new_route, const Route& existing_route, int deciding_asn) const {

    // ROV filtering
    if (rov_enabled_asns.count(deciding_asn) > 0 && new_route.rov_invalid != existing_route.rov_invalid) {
        return !new_route.rov_invalid;
    }
    
    // Relationship preference
    int new_pref = (new_route.announcement_type == AnnouncementType::LEARNED_FROM_CUSTOMER) ? 2 :
                   (new_route.announcement_type == AnnouncementType::LEARNED_FROM_PEER) ? 1 : 0;
    int existing_pref = (existing_route.announcement_type == AnnouncementType::LEARNED_FROM_CUSTOMER) ? 2 :
                        (existing_route.announcement_type == AnnouncementType::LEARNED_FROM_PEER) ? 1 : 0;
    
    if (new_pref != existing_pref) {
        return new_pref > existing_pref;
    }
    
    // AS path length
    if (new_route.as_path.size() != existing_route.as_path.size()) {
        return new_route.as_path.size() < existing_route.as_path.size();
    }
    
    // Tie-breaker: next-hop ASN
    int new_next_hop = (new_route.as_path.size() >= 2) ? new_route.as_path[1] : new_route.as_path[0];
    int existing_next_hop = (existing_route.as_path.size() >= 2) ? existing_route.as_path[1] : existing_route.as_path[0];
    
    bool result = new_next_hop < existing_next_hop;
    
    
    return result;
}

void BGPSimulator::send_route_to_neighbor(int sender_asn, int receiver_asn,
                                          const Route& route, RelationType relationship) {

    if (std::find(route.as_path.begin(), route.as_path.end(), receiver_asn) != route.as_path.end()) {
        return;
    }

    if (!can_export(route, relationship)) {
        return;
    }

    Route sent_route = route.copy();
    sent_route.prepend(receiver_asn);
    sent_route.announcement_type = relationship_to_announcement_type(relationship);

    message_queues[receiver_asn][route.prefix].push_back(std::make_shared<Route>(sent_route));
}

void BGPSimulator::process_messages(int asn) {
    auto queue_it = message_queues.find(asn);
    if (queue_it == message_queues.end()) {
        return;
    }
    
    for (const auto& prefix_entry : queue_it->second) {
        const std::string& prefix = prefix_entry.first;
        const auto& routes = prefix_entry.second;
        
        for (const auto& route : routes) {
            // ROV check: drop invalid routes at ROV-enabled ASNs
            if (rov_enabled_asns.count(asn) > 0 && route->rov_invalid) {
                continue;
            }
            
            auto rib_it = ribs.find(asn);
            if (rib_it == ribs.end() || rib_it->second.find(prefix) == rib_it->second.end()) {
                ribs[asn][prefix] = route;
            } else {
                auto existing_route = rib_it->second[prefix];
                if (better_route(*route, *existing_route, asn)) {
                    ribs[asn][prefix] = route;
                }
            }
        }
    }
    
    message_queues[asn].clear();
}

bool BGPSimulator::propagate() {
    std::cout << "Starting BGP propagation...\n";
    flatten_graph();
    
    int iteration = 0;
    int prev_total_routes = 0;
    
    while (true) {
        iteration++;
        std::cout << "Iteration " << iteration << ":\n";
        
        // Phase 1: Customers send to providers (UP)
        std::cout << "  Phase 1: Propagating to providers...\n";
        for (int rank = 0; rank < (int)rank_to_asns.size(); ++rank) {
            // send from this rank
            for (int asn : rank_to_asns[rank]) {
                auto rib_it = ribs.find(asn);
                if (rib_it != ribs.end()) {
                    for (const auto& [prefix, routePtr] : rib_it->second) {
                        const Route& route = *routePtr;
                        for (const auto& [nbr_asn, rel] : graph.get_neighbors(asn)) {
                            if (rel == RelationType::CUSTOMER_TO_PROVIDER) {
                                send_route_to_neighbor(asn, nbr_asn, route, rel);
                            }
                        }
                    }
                }
            }
            // process the **next** rank (providers)
            if (rank + 1 < (int)rank_to_asns.size()) {
                for (int asn : rank_to_asns[rank + 1]) {
                    process_messages(asn);
                }
            }
        }
        
        // Phase 2: Peers send to peers  
        std::cout << "  Phase 2: Propagating to peers...\n";
        for (int rank = 0; rank < static_cast<int>(rank_to_asns.size()); rank++) {
            for (int asn : rank_to_asns[rank]) {
                auto rib_it = ribs.find(asn);
                if (rib_it != ribs.end()) {
                    for (const auto& route_entry : rib_it->second) {
                        const auto& route = route_entry.second;
                        for (const auto& neighbor : graph.get_neighbors(asn)) {
                            if (neighbor.second == RelationType::PEER_TO_PEER) {
                                send_route_to_neighbor(asn, neighbor.first, *route, neighbor.second);
                            }
                        }
                    }
                }
            }
            for (int asn : rank_to_asns[rank]) {
                process_messages(asn);
            }
        }
        
        // Phase 3: Providers send to customers (DOWN)
        std::cout << "  Phase 3: Propagating to customers...\n";
        for (int rank = (int)rank_to_asns.size() - 1; rank >= 0; --rank) {
            // send from this rank
            for (int asn : rank_to_asns[rank]) {
                auto rib_it = ribs.find(asn);
                if (rib_it != ribs.end()) {
                    for (const auto& [prefix, routePtr] : rib_it->second) {
                        const Route& route = *routePtr;
                        for (const auto& [nbr_asn, rel] : graph.get_neighbors(asn)) {
                            if (rel == RelationType::PROVIDER_TO_CUSTOMER) {
                                send_route_to_neighbor(asn, nbr_asn, route, rel);
                            }
                        }
                    }
                }
            }
            // process the **previous** rank (customers)
            if (rank > 0) {
                for (int asn : rank_to_asns[rank - 1]) {
                    process_messages(asn);
                }
            }
        }
        
        int total_routes = 0;
        for (const auto& asn_entry : ribs) {
            total_routes += asn_entry.second.size();
        }
        
        std::cout << "  Total routes: " << total_routes << "\n";
        
        if (total_routes == prev_total_routes) {
            std::cout << "BGP converged after " << iteration << " iterations!\n";
            return true;
        }
        
        prev_total_routes = total_routes;
        
        if (iteration >= 20) {
            std::cout << "Error: BGP propagation did not converge after 20 iterations - possible routing cycle detected!\n";
            return false;
        }
    }
}

void BGPSimulator::export_ribs_csv(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not create output file: " + filename);
    }
    
    file << "asn,prefix,as_path\n";
    
    std::vector<std::tuple<int, std::string, std::string>> entries;
    
    for (const auto& asn_entry : ribs) {
        int asn = asn_entry.first;
        for (const auto& route_entry : asn_entry.second) {
            const std::string& prefix = route_entry.first;
            const auto& route = route_entry.second;
            
            std::ostringstream path_ss;
            path_ss << "(";
            for (size_t i = 0; i < route->as_path.size(); i++) {
                if (i > 0) path_ss << ", ";
                path_ss << route->as_path[i];
            }
            if (route->as_path.size() == 1) {
                path_ss << ",";
            }
            path_ss << ")";
            std::string path_str = path_ss.str();
            
            entries.push_back(std::make_tuple(asn, prefix, path_str));
        }
    }
    
    std::sort(entries.begin(), entries.end());
    
    for (const auto& entry : entries) {
        file << std::get<0>(entry) << "," << std::get<1>(entry) << ",\"" << std::get<2>(entry) << "\"\n";
    }
}

int BGPSimulator::get_rib_count() const {
    int count = 0;
    for (const auto& asn_entry : ribs) {
        count += asn_entry.second.size();
    }
    return count;
}