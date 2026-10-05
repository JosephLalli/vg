#include "catch.hpp"
#include "../algorithms/prune.hpp"

#include <bdsg/hash_graph.hpp>
#include <bdsg/packed_graph.hpp>

namespace vg {
namespace unittest {

template<typename Graph>
static void check_prune_edges(bool with_tips) {
    CAPTURE(with_tips);
    Graph graph;
    for (nid_t id = 1; id <= 7; ++id) {
        graph.create_handle("A", id);
    }
    auto first = graph.get_handle(1);
    auto reversed = graph.get_handle(2, true);
    auto other = graph.get_handle(3);
    auto join = graph.get_handle(4);
    graph.create_edge(first, reversed);
    graph.create_edge(first, other);
    graph.create_edge(reversed, join);
    graph.create_edge(other, join);
    graph.create_edge(join, graph.get_handle(5));
    graph.create_edge(graph.get_handle(6), graph.get_handle(6));
    graph.create_edge(graph.get_handle(7), graph.get_handle(7, true));
    auto path = graph.create_path_handle("reversed_branch");
    for (const auto& handle : {first, reversed, join}) {
        graph.append_step(path, handle);
    }

    if (with_tips) {
        algorithms::prune_complex_with_head_tail(graph, 3, 0);
    } else {
        REQUIRE(algorithms::prune_complex(graph, 3, 0) == 4);
    }
    REQUIRE(graph.get_node_count() == 7);
    REQUIRE(graph.get_edge_count() == (with_tips ? 2 : 3));
    REQUIRE_FALSE(graph.has_edge(first, reversed));
    REQUIRE_FALSE(graph.has_edge(first, other));
    REQUIRE_FALSE(graph.has_edge(reversed, join));
    REQUIRE_FALSE(graph.has_edge(other, join));
    REQUIRE(graph.has_edge(join, graph.get_handle(5)));
    // Breaking the tipless cycle adds an overlay branch, so its loop is pruned.
    REQUIRE(graph.has_edge(graph.get_handle(6), graph.get_handle(6)) == !with_tips);
    REQUIRE(graph.has_edge(graph.get_handle(7), graph.get_handle(7, true)));
    std::vector<handle_t> steps;
    graph.for_each_step_in_path(path, [&](const step_handle_t& step) {
        steps.push_back(graph.get_handle_of_step(step));
    });
    REQUIRE(steps == std::vector<handle_t>{first, reversed, join});

    Graph empty;
    REQUIRE(algorithms::prune_complex(empty, 3, 0) == 0);
    REQUIRE(algorithms::prune_complex_with_head_tail(empty, 3, 0) == 0);
}

TEST_CASE("Pruning preserves oriented surviving edges and stored paths", "[prune][indexing]") {
    for (bool with_tips : {false, true}) {
        check_prune_edges<bdsg::PackedGraph>(with_tips);
        check_prune_edges<bdsg::MappedPackedGraph>(with_tips);
        check_prune_edges<bdsg::HashGraph>(with_tips);
    }
}

} // namespace unittest
} // namespace vg
