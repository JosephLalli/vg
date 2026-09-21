#include <bdsg/packed_graph.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace handlegraph;

// This compares node IDs as well as topology. It deliberately requires pathless
// graphs; transcript graphs use the separate named-walk fingerprint protocol.
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: compare_pathless_graphs CONTROL.pg CANDIDATE.pg\n";
        return 2;
    }
    try {
        bdsg::PackedGraph control, candidate;
        std::cerr << "Loading control graph\n";
        control.deserialize(argv[1]);
        std::cerr << "Loading candidate graph\n";
        candidate.deserialize(argv[2]);
        if (control.get_path_count() != 0 || candidate.get_path_count() != 0) {
            throw std::runtime_error("comparison requires two pathless graphs");
        }
        if (control.get_node_count() != candidate.get_node_count()) {
            throw std::runtime_error("node counts differ");
        }
        uint64_t nodes = 0, bases = 0, outgoing = 0;
        using Neighbor = std::pair<nid_t, bool>;
        std::vector<Neighbor> expected, observed;
        control.for_each_handle([&](const handle_t& forward) {
            const nid_t id = control.get_id(forward);
            if (!candidate.has_node(id)) {
                throw std::runtime_error("candidate lacks node " + std::to_string(id));
            }
            const handle_t other = candidate.get_handle(id);
            const std::string sequence = control.get_sequence(forward);
            if (candidate.get_sequence(other) != sequence) {
                throw std::runtime_error("sequence differs at node " + std::to_string(id));
            }
            bases += sequence.size();
            for (bool reverse : {false, true}) {
                expected.clear();
                observed.clear();
                const handle_t first = reverse ? control.flip(forward) : forward;
                const handle_t second = reverse ? candidate.flip(other) : other;
                control.follow_edges(first, false, [&](const handle_t& next) {
                    expected.emplace_back(control.get_id(next), control.get_is_reverse(next));
                });
                candidate.follow_edges(second, false, [&](const handle_t& next) {
                    observed.emplace_back(candidate.get_id(next), candidate.get_is_reverse(next));
                });
                std::sort(expected.begin(), expected.end());
                std::sort(observed.begin(), observed.end());
                if (expected != observed) {
                    throw std::runtime_error("outgoing edges differ at node " + std::to_string(id) +
                                             (reverse ? " reverse" : " forward"));
                }
                outgoing += expected.size();
            }
            ++nodes;
            if (nodes % 1000000 == 0) {
                std::cerr << "Compared " << nodes << " nodes\n";
            }
        });
        std::cout << "{\"passed\":true,\"nodes\":" << nodes
                  << ",\"sequence_bases\":" << bases
                  << ",\"oriented_outgoing_entries\":" << outgoing
                  << ",\"paths\":0,\"node_ids_equal\":true,\"node_sequences_equal\":true,"
                     "\"oriented_adjacency_equal\":true}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "compare_pathless_graphs: " << error.what() << '\n';
        return 1;
    }
}
