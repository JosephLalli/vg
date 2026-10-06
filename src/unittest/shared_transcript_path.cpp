#include <memory>
#include <sstream>
#include <vector>
#include "bdsg/hash_graph.hpp"
#include "bdsg/packed_graph.hpp"
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

static unique_ptr<Transcriptome> make_output_transcriptome(const bool add_existing_path = false, const size_t steps = 2) {
    auto graph = make_unique<bdsg::PackedGraph>();
    const handle_t one = graph->create_handle("AAAA", 1);
    const handle_t two = graph->create_handle("CCCC", 2);
    graph->create_edge(one, two);
    if (add_existing_path) {
        graph->create_path_handle("existing");
    }
    auto transcriptome = make_unique<Transcriptome>(std::move(graph));

    auto& paths = const_cast<vector<CompletedTranscriptPath>&>(transcriptome->transcript_paths());
    EditedTranscriptPath owned("owned", "source", true, false);
    for (size_t i = 0; i < steps; ++i) {
        owned.path.push_back({i % 2 ? two : one, 0, 4});
    }
    paths.emplace_back(owned, transcriptome->graph());

    using Source = SharedTranscriptPath<EditedMapping>::Source;
    vector<EditedMapping> mappings;
    for (size_t i = 0; i < steps; ++i) {
        mappings.push_back({i % 2 ? one : two, 0, 4});
    }
    auto source = make_shared<Source>(std::move(mappings));
    EditedTranscriptPath shared("shared", "source", true, false);
    shared.shared_path.append(source, 0, steps, 0, 4);
    paths.emplace_back(shared, transcriptome->graph());

    EditedTranscriptPath haplotype("haplotype", "source", false, true);
    haplotype.path = {{two, 0, 4}};
    paths.emplace_back(haplotype, transcriptome->graph());

    return transcriptome;
}

TEST_CASE("Transcriptome parallel output preserves ordinary path bytes", "[transcriptome]") {
    auto embedded = make_output_transcriptome(false, 1024);
    embedded->embed_transcript_paths(true, false);
    stringstream expected;
    embedded->write_graph(&expected);
    for (int32_t threads : {1, 2, 4}) {
        auto generated = make_output_transcriptome(false, 1024);
        generated->num_threads = threads;
        stringstream output;
        generated->write_graph_with_transcript_paths(&output, true, false);
        REQUIRE(generated->graph().get_path_count() == 0);
        REQUIRE(output.str() == expected.str());
    }
}

TEST_CASE("Transcriptome writes eligible PackedGraph paths directly", "[transcriptome]") {
    auto embedded = make_output_transcriptome();
    embedded->embed_transcript_paths(true, false);
    REQUIRE(embedded->graph().get_path_count() == 2);
    stringstream expected;
    embedded->write_graph(&expected);

    auto generated = make_output_transcriptome();
    stringstream generated_output;
    generated->write_graph_with_transcript_paths(&generated_output, true, false);
    REQUIRE(generated->graph().get_path_count() == 0);
    REQUIRE(generated_output.str() == expected.str());

    bdsg::PackedGraph restored;
    stringstream restored_input(generated_output.str());
    restored.deserialize(restored_input);
    REQUIRE(restored.has_path("owned_R1"));
    REQUIRE(restored.has_path("shared_R1"));
    REQUIRE(!restored.has_path("haplotype_H1"));
    REQUIRE(restored.get_path_count() == 2);

    auto fallback = make_output_transcriptome(true);
    stringstream fallback_output;
    fallback->write_graph_with_transcript_paths(&fallback_output, true, false);
    REQUIRE(fallback->graph().get_path_count() == 3);
    REQUIRE(fallback->graph().has_path("owned_R1"));
    REQUIRE(fallback->graph().has_path("shared_R1"));
}
} }
