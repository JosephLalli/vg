#include "catch.hpp"
#include "../source_sink_overlay.hpp"
#include <bdsg/packed_graph.hpp>

namespace vg {
namespace unittest {

TEST_CASE("SourceSinkOverlay discovers tips across separate components", "[overlay][components]") {
    bdsg::PackedGraph graph;
    auto left = graph.create_handle("A", 1);
    auto right = graph.create_handle("C", 2);
    auto isolated = graph.create_handle("G", 3);

    SECTION("ordinary edge") {
        graph.create_edge(left, right);
    }
    SECTION("reversing edge") {
        graph.create_edge(left, graph.flip(right));
    }
    SECTION("isolated nodes") {
    }

    SourceSinkOverlay overlay(&graph, 4, 10, 11);
    std::unordered_set<handle_t> heads, tails;
    overlay.follow_edges(overlay.get_source_handle(), false, [&](const handle_t& handle) {
        heads.insert(overlay.get_underlying_handle(handle));
    });
    overlay.follow_edges(overlay.get_sink_handle(), true, [&](const handle_t& handle) {
        tails.insert(overlay.get_underlying_handle(handle));
    });
    graph.for_each_handle([&](const handle_t& handle) {
        REQUIRE(heads.count(handle) == (graph.get_degree(handle, true) == 0));
        REQUIRE(tails.count(handle) == (graph.get_degree(handle, false) == 0));
    });
}

TEST_CASE("SourceSinkOverlay handles empty and tipless components", "[overlay][components]") {
    bdsg::PackedGraph graph;
    SECTION("empty graph") {
        SourceSinkOverlay overlay(&graph, 4, 10, 11);
        REQUIRE(overlay.get_degree(overlay.get_source_handle(), false) == 0);
        REQUIRE(overlay.get_degree(overlay.get_sink_handle(), true) == 0);
    }
    SECTION("cycle remains disconnected when requested") {
        auto node = graph.create_handle("A", 1);
        graph.create_edge(node, node);
        SourceSinkOverlay overlay(&graph, 4, 10, 11, false);
        REQUIRE(overlay.get_degree(overlay.get_source_handle(), false) == 0);
        REQUIRE(overlay.get_degree(overlay.get_sink_handle(), true) == 0);
    }
    SECTION("one representative per cycle") {
        for (handlegraph::nid_t id : {1, 2}) {
            auto node = graph.create_handle("A", id);
            graph.create_edge(node, node);
        }
        SourceSinkOverlay overlay(&graph, 4, 10, 11);
        REQUIRE(overlay.get_degree(overlay.get_source_handle(), false) == 2);
        REQUIRE(overlay.get_degree(overlay.get_sink_handle(), true) == 2);
    }
}

TEST_CASE("SourceSinkOverlay handles dense and sparse ID spans", "[overlay][components]") {
    bdsg::PackedGraph graph;
    SECTION("dense IDs cross bitmap word boundaries") {
        for (handlegraph::nid_t id = 1; id <= 129; ++id) {
            graph.create_handle("A", id);
        }
    }
    SECTION("sparse IDs do not require a max-ID allocation") {
        graph.create_handle("A", 1);
        graph.create_handle("C", 1000000000000LL);
    }
    SourceSinkOverlay overlay(&graph, 4, graph.max_node_id() + 1, graph.max_node_id() + 2);
    REQUIRE(overlay.get_degree(overlay.get_source_handle(), false) == graph.get_node_count());
    REQUIRE(overlay.get_degree(overlay.get_sink_handle(), true) == graph.get_node_count());
}

}
}
