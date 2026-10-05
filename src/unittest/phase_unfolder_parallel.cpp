/** \file
 * Unit tests for deterministic parallel component unfolding.
 */

#include <algorithm>
#include <fstream>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include <omp.h>

#include <bdsg/hash_graph.hpp>

#include "../gbwt_helper.hpp"
#include "../phase_unfolder.hpp"
#include "../utility.hpp"
#include "catch.hpp"

namespace vg {
namespace unittest {
namespace {

using node_record_t = std::tuple<vg::id_t, std::string>;
using edge_record_t = std::tuple<vg::id_t, bool, vg::id_t, bool>;
using mapping_record_t = std::pair<vg::id_t, vg::id_t>;

struct UnfoldSnapshot {
    std::vector<node_record_t> nodes;
    std::vector<edge_record_t> edges;
    std::vector<mapping_record_t> mappings;

    bool operator==(const UnfoldSnapshot& other) const {
        return (this->nodes == other.nodes && this->edges == other.edges &&
                this->mappings == other.mappings);
    }
};

class OmpSettingsGuard {
public:
    OmpSettingsGuard() : threads(omp_get_max_threads()), dynamic(omp_get_dynamic()) {
        omp_set_dynamic(0);
    }

    ~OmpSettingsGuard() {
        omp_set_num_threads(this->threads);
        omp_set_dynamic(this->dynamic);
    }

private:
    int threads;
    int dynamic;
};

vg::id_t component_node(size_t component, size_t offset) {
    return static_cast<vg::id_t>(4 * component + offset);
}

template<class Graph>
void build_source_graph(Graph& graph, std::vector<gbwt::vector_type>& gbwt_paths,
                        size_t component_count) {
    for (size_t component = 0; component < component_count; component++) {
        vg::id_t left_id = component_node(component, 1);
        vg::id_t reference_id = component_node(component, 2);
        vg::id_t haplotype_id = component_node(component, 3);
        vg::id_t right_id = component_node(component, 4);
        bool haplotype_reverse = (component % 2 != 0);

        handle_t left = graph.create_handle("A", left_id);
        handle_t reference = graph.create_handle("C", reference_id);
        handle_t haplotype = graph.create_handle("G", haplotype_id);
        handle_t right = graph.create_handle("T", right_id);
        if (haplotype_reverse) {
            haplotype = graph.flip(haplotype);
        }
        graph.create_edge(left, reference);
        graph.create_edge(reference, right);
        graph.create_edge(left, haplotype);
        graph.create_edge(haplotype, right);

        path_handle_t path = graph.create_path_handle("reference_" + std::to_string(component));
        graph.append_step(path, left);
        graph.append_step(path, reference);
        graph.append_step(path, right);

        gbwt_paths.push_back({
            gbwt::Node::encode(left_id, false),
            gbwt::Node::encode(haplotype_id, haplotype_reverse),
            gbwt::Node::encode(right_id, false)
        });
    }
}

bdsg::HashGraph make_pruned_graph(size_t component_count) {
    bdsg::HashGraph graph;
    for (size_t component = 0; component < component_count; component++) {
        graph.create_handle("A", component_node(component, 1));
        graph.create_handle("T", component_node(component, 4));
    }
    return graph;
}

UnfoldSnapshot snapshot(const HandleGraph& graph, const PhaseUnfolder& unfolder,
                        vg::id_t first_mapping) {
    UnfoldSnapshot result;
    graph.for_each_handle([&](const handle_t& handle) {
        result.nodes.emplace_back(graph.get_id(handle), graph.get_sequence(handle));
    });
    graph.for_each_edge([&](const edge_t& edge) {
        result.edges.emplace_back(graph.get_id(edge.first), graph.get_is_reverse(edge.first),
                                  graph.get_id(edge.second), graph.get_is_reverse(edge.second));
    });
    std::sort(result.nodes.begin(), result.nodes.end());
    std::sort(result.edges.begin(), result.edges.end());
    for (vg::id_t duplicate = first_mapping; duplicate <= graph.max_node_id(); duplicate++) {
        result.mappings.emplace_back(duplicate, unfolder.get_mapping(duplicate));
    }
    return result;
}

vg::id_t find_copy(const HandleGraph& graph, const PhaseUnfolder& unfolder,
                   vg::id_t first_mapping, vg::id_t original) {
    std::vector<vg::id_t> copies;
    graph.for_each_handle([&](const handle_t& handle) {
        vg::id_t id = graph.get_id(handle);
        if (id >= first_mapping && unfolder.get_mapping(id) == original) {
            copies.push_back(id);
        }
    });
    REQUIRE(copies.size() == 1);
    return copies.front();
}

void check_fixture(const bdsg::HashGraph& source, const HandleGraph& graph,
                   const PhaseUnfolder& unfolder, vg::id_t first_mapping,
                   size_t component_count) {
    REQUIRE(graph.get_node_count() == 4 * component_count);
    REQUIRE(graph.get_edge_count() == 4 * component_count);
    for (size_t component = 0; component < component_count; component++) {
        vg::id_t left_id = component_node(component, 1);
        vg::id_t reference_id = component_node(component, 2);
        vg::id_t haplotype_id = component_node(component, 3);
        vg::id_t right_id = component_node(component, 4);
        bool haplotype_reverse = (component % 2 != 0);
        vg::id_t reference_copy = find_copy(graph, unfolder, first_mapping, reference_id);
        vg::id_t haplotype_copy = find_copy(graph, unfolder, first_mapping, haplotype_id);
        handle_t haplotype_handle = graph.get_handle(haplotype_copy, haplotype_reverse);

        REQUIRE(graph.get_sequence(graph.get_handle(reference_copy)) ==
                source.get_sequence(source.get_handle(reference_id)));
        REQUIRE(graph.get_sequence(graph.get_handle(haplotype_copy)) ==
                source.get_sequence(source.get_handle(haplotype_id)));
        REQUIRE(graph.has_edge(graph.get_handle(left_id), graph.get_handle(reference_copy)));
        REQUIRE(graph.has_edge(graph.get_handle(reference_copy), graph.get_handle(right_id)));
        REQUIRE(graph.has_edge(graph.get_handle(left_id), haplotype_handle));
        REQUIRE(graph.has_edge(haplotype_handle, graph.get_handle(right_id)));
    }
}

UnfoldSnapshot run_fixture(const bdsg::HashGraph& source, const gbwt::GBWT& gbwt_index,
                           size_t component_count, vg::id_t first_mapping,
                           const std::string& mapping_input = "",
                           const std::string& mapping_output = "") {
    bdsg::HashGraph graph = make_pruned_graph(component_count);
    PhaseUnfolder unfolder(source, gbwt_index, first_mapping);
    if (!mapping_input.empty()) {
        unfolder.read_mapping(mapping_input);
    }
    unfolder.unfold(graph);
    REQUIRE(unfolder.verify_paths(graph) == 0);
    check_fixture(source, graph, unfolder, first_mapping, component_count);
    if (!mapping_output.empty()) {
        unfolder.write_mapping(mapping_output);
    }
    return snapshot(graph, unfolder, first_mapping);
}

std::multiset<vg::id_t> mapping_values(const UnfoldSnapshot& snapshot) {
    std::multiset<vg::id_t> result;
    for (const mapping_record_t& mapping : snapshot.mappings) {
        result.insert(mapping.second);
    }
    return result;
}

std::multiset<vg::id_t> expected_mapping_values(size_t component_count, size_t copies) {
    std::multiset<vg::id_t> result;
    for (size_t component = 0; component < component_count; component++) {
        for (size_t copy = 0; copy < copies; copy++) {
            result.insert(component_node(component, 2));
            result.insert(component_node(component, 3));
        }
    }
    return result;
}

class ThrowingPathGraph : public bdsg::HashGraph {
public:
    bool for_each_step_on_handle_impl(
        const handle_t&, const std::function<bool(const step_handle_t&)>&) const override {
        throw std::runtime_error("intentional path query failure");
    }
};

} // namespace

TEST_CASE("PhaseUnfolder preserves graph and mapping identity across component workers",
          "[phaseunfolder][parallel]") {
    OmpSettingsGuard omp_settings;
    const size_t component_count = 40;
    const vg::id_t first_mapping = component_node(component_count - 1, 4) + 1;
    bdsg::HashGraph source;
    std::vector<gbwt::vector_type> gbwt_paths;
    build_source_graph(source, gbwt_paths, component_count);
    gbwt::GBWT gbwt_index = get_gbwt(gbwt_paths);

    std::string mapping_file = temp_file::create("phase-unfolder-parallel");
    UnfoldSnapshot expected;
    for (int threads : { 1, 2, 4 }) {
        omp_set_num_threads(threads);
        UnfoldSnapshot observed = run_fixture(source, gbwt_index, component_count, first_mapping,
                                              "", (threads == 1 ? mapping_file : ""));
        if (threads == 1) {
            expected = observed;
            REQUIRE(expected.mappings.size() == 2 * component_count);
            REQUIRE(mapping_values(expected) == expected_mapping_values(component_count, 1));
        } else {
            REQUIRE(observed == expected);
        }
    }

    std::ifstream mapping_stream(mapping_file, std::ios::binary | std::ios::ate);
    REQUIRE(mapping_stream.tellg() > 0);
    mapping_stream.close();

    UnfoldSnapshot expected_appended;
    const vg::id_t appended_begin = first_mapping + 2 * component_count;
    for (int threads : { 1, 2, 4 }) {
        omp_set_num_threads(threads);
        UnfoldSnapshot observed = run_fixture(source, gbwt_index, component_count, first_mapping,
                                              mapping_file);
        if (threads == 1) {
            expected_appended = observed;
            REQUIRE(expected_appended.mappings.size() == 4 * component_count);
            REQUIRE(mapping_values(expected_appended) == expected_mapping_values(component_count, 2));
            for (const node_record_t& node : expected_appended.nodes) {
                vg::id_t id = std::get<0>(node);
                if (id >= first_mapping) {
                    REQUIRE(id >= appended_begin);
                }
            }
        } else {
            REQUIRE(observed == expected_appended);
        }
    }
    temp_file::remove(mapping_file);
}

TEST_CASE("PhaseUnfolder accepts an empty graph", "[phaseunfolder][parallel]") {
    OmpSettingsGuard omp_settings;
    omp_set_num_threads(4);
    bdsg::HashGraph source, graph;
    gbwt::GBWT gbwt_index;
    PhaseUnfolder unfolder(source, gbwt_index, 1);
    unfolder.unfold(graph);
    REQUIRE(graph.get_node_count() == 0);
    REQUIRE(graph.get_edge_count() == 0);
}

TEST_CASE("PhaseUnfolder rethrows component worker failures", "[phaseunfolder][parallel]") {
    OmpSettingsGuard omp_settings;
    omp_set_num_threads(4);
    const size_t component_count = 8;
    ThrowingPathGraph source;
    std::vector<gbwt::vector_type> gbwt_paths;
    build_source_graph(source, gbwt_paths, component_count);
    gbwt::GBWT gbwt_index = get_gbwt(gbwt_paths);
    bdsg::HashGraph graph = make_pruned_graph(component_count);
    vg::id_t first_mapping = component_node(component_count - 1, 4) + 1;
    PhaseUnfolder unfolder(source, gbwt_index, first_mapping);

    REQUIRE_THROWS_AS(unfolder.unfold(graph), std::runtime_error);
    REQUIRE(graph.get_node_count() == 2 * component_count);
    REQUIRE(graph.get_edge_count() == 0);
}

} // namespace unittest
} // namespace vg
