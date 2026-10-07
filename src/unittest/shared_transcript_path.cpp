#include <limits>
#include <memory>
#include <vector>
#include "bdsg/hash_graph.hpp"
#include "../transcriptome.hpp"
#include "catch.hpp"
namespace vg { namespace unittest {
static std::vector<EditedMapping> shared_mappings(const EditedTranscriptPath& path, const HandleGraph& graph) {
    std::vector<EditedMapping> result;
    path.for_each_mapping(graph, [&](const EditedMapping& mapping, uint64_t rank) {
        REQUIRE(rank == result.size());
        result.push_back(mapping);
    });
    return result;
}
using namespace std;
TEST_CASE("Shared edited transcript paths expand with graph orientation", "[transcriptome][shared_transcript_path]") {
    auto graph = make_unique<bdsg::HashGraph>();
    const handle_t one = graph->create_handle("AAAA", 1);
    const handle_t two = graph->create_handle("CCCCC", 2);

    using Source = SharedTranscriptPath<EditedMapping>::Source;
    auto source = make_shared<Source>(vector<EditedMapping>{
        {one, 0, 4}, {graph->flip(two), 0, 5}, {one, 0, 4}
    });

    EditedTranscriptPath whole("whole", gbwt::Path::id(0), true, false);
    whole.shared_path.append(source, 0, 3, 0, 4);
    const CompletedTranscriptPath completed(whole, *graph);
    REQUIRE(completed.path.empty());
    REQUIRE(!completed.shared_path.empty());
    vector<handle_t> completed_walk;
    completed.for_each_handle(*graph, [&](const handle_t handle, uint64_t rank) {
        REQUIRE(rank == completed_walk.size());
        completed_walk.push_back(handle);
    });
    REQUIRE(completed_walk == vector<handle_t>{one, graph->flip(two), one});
    REQUIRE(completed.get_first_node_handle(*graph) == one);
    REQUIRE(completed.resident_size() == completed_walk.size());

    EditedTranscriptPath partial("partial", gbwt::Path::id(0), true, false);
    partial.shared_path.append(source, 0, 3, 1, 3);
    REQUIRE(shared_mappings(partial, *graph) == vector<EditedMapping>{
        {one, 1, 3}, {graph->flip(two), 0, 5}, {one, 0, 3}
    });
    partial.shared_path.reverse_complement();
    REQUIRE(shared_mappings(partial, *graph) == vector<EditedMapping>{
        {graph->flip(one), 1, 3}, {two, 0, 5}, {graph->flip(one), 0, 3}
    });
}


TEST_CASE("Shared slices validate boundaries and preserve copied vector API", "[shared_transcript_path]") {
    bdsg::HashGraph graph;
    auto one = graph.create_handle("AAAA", 1);
    auto source = make_shared<SharedTranscriptPath<EditedMapping>::Source>(vector<EditedMapping>{{one, 0, 4}});
    EditedTranscriptPath clipped("clipped", gbwt::Path::id(0), true, false);
    REQUIRE_THROWS_AS(clipped.shared_path.append(source, 0, 1, 3, 2), invalid_argument);
    REQUIRE_THROWS_AS(clipped.shared_path.append(source, 0, 2, 0, 4), invalid_argument);
    clipped.shared_path.append(source, 0, 1, 1, 3);
    REQUIRE_THROWS_AS(CompletedTranscriptPath(clipped, graph), logic_error);
    EditedTranscriptPath whole("whole", gbwt::Path::id(0), true, false);
    whole.shared_path.append(source, 0, 1, 0, 4);
    CompletedTranscriptPath copy(whole, graph);
    copy.materialize(graph);
    REQUIRE(copy.path == vector<handle_t>{one});
    REQUIRE(copy.shared_path.empty());
    REQUIRE(!whole.shared_path.empty());
}

TEST_CASE("Shared edited paths preserve wide node coordinates", "[shared_transcript_path][edited_mapping]") {
    bdsg::HashGraph graph;
    const auto one = graph.create_handle("A", 1);
    const auto two = graph.create_handle("C", 2);
    using SharedPath = SharedTranscriptPath<EditedMapping>;

    const int64_t first_offset = static_cast<int64_t>(numeric_limits<uint32_t>::max()) + 17;
    const uint64_t last_end = static_cast<uint64_t>(first_offset) + 31;
    const uint64_t node_length = last_end + 101;
    auto wide_source = make_shared<SharedPath::Source>(vector<SharedPath::SourceStep>{
        {one, node_length}
    });

    SharedPath clipped;
    clipped.append(wide_source, 0, 1, first_offset, last_end);
    vector<EditedMapping> mappings;
    clipped.for_each_mapping([&](const handle_t& handle) { return graph.flip(handle); },
                             [&](const EditedMapping& mapping, uint64_t) {
        mappings.push_back(mapping);
    });
    REQUIRE(mappings == vector<EditedMapping>{{one, first_offset, 31}});

    clipped.reverse_complement();
    mappings.clear();
    clipped.for_each_mapping([&](const handle_t& handle) { return graph.flip(handle); },
                             [&](const EditedMapping& mapping, uint64_t) {
        mappings.push_back(mapping);
    });
    REQUIRE(mappings == vector<EditedMapping>{
        {graph.flip(one), static_cast<int64_t>(node_length - last_end), 31}
    });

    SharedPath unrepresentable_whole_node;
    REQUIRE_THROWS_AS(unrepresentable_whole_node.append(
        wide_source, 0, 1, 0, node_length), invalid_argument);

    const uint64_t maximum_edit_length = numeric_limits<int32_t>::max();
    auto maximum_length_source = make_shared<SharedPath::Source>(vector<SharedPath::SourceStep>{
        {two, maximum_edit_length}
    });
    SharedPath maximum_length_path;
    maximum_length_path.append(maximum_length_source, 0, 1, 0, maximum_edit_length);
    mappings.clear();
    maximum_length_path.for_each_mapping(
        [&](const handle_t& handle) { return graph.flip(handle); },
        [&](const EditedMapping& mapping, uint64_t) { mappings.push_back(mapping); });
    REQUIRE(mappings == vector<EditedMapping>{{two, 0, numeric_limits<int32_t>::max()}});
}
} }
