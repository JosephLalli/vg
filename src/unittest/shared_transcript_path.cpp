// Unit tests for the in-memory shared representation of GBWT reference paths.

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "bdsg/packed_graph.hpp"
#include "gbwt/dynamic_gbwt.h"

#include "../transcriptome.hpp"

#include "catch.hpp"

namespace vg {
namespace unittest {

using namespace std;

static vector<EditedMapping> shared_mappings(const EditedTranscriptPath& path,
                                             const HandleGraph& graph) {
    vector<EditedMapping> mappings;
    path.for_each_mapping(graph, [&](const EditedMapping& mapping, uint64_t rank) {
        REQUIRE(rank == mappings.size());
        mappings.push_back(mapping);
    });
    return mappings;
}

static unique_ptr<gbwt::GBWT> shared_test_index() {
    gbwt::Verbosity::set(gbwt::Verbosity::SILENT);
    gbwt::GBWTBuilder builder(gbwt::bit_length(gbwt::Node::encode(4, true)));
    builder.index.addMetadata();

    // Two complete sample1 copies have the same terminal node. The first
    // repeats node 2 and includes node 3 in reverse orientation.
    const gbwt::vector_type first = {
        gbwt::Node::encode(1, false), gbwt::Node::encode(2, false),
        gbwt::Node::encode(3, true), gbwt::Node::encode(2, false),
        gbwt::Node::encode(4, false)
    };
    const gbwt::vector_type second = {
        gbwt::Node::encode(1, false), gbwt::Node::encode(2, false),
        gbwt::Node::encode(4, false)
    };
    // sample2#0#path2 is represented by two fragments with a coordinate gap:
    // [0, 7) and [10, 20). An exon may bridge the gap; a single exon may not.
    const gbwt::vector_type fragment_first = {
        gbwt::Node::encode(1, false), gbwt::Node::encode(2, false)
    };
    const gbwt::vector_type fragment_second = {
        gbwt::Node::encode(3, true), gbwt::Node::encode(2, false),
        gbwt::Node::encode(4, false)
    };
    builder.insert(first, true);
    builder.index.metadata.addPath(0, 0, 0, 0);
    builder.insert(second, true);
    builder.index.metadata.addPath(0, 0, 1, 0);
    builder.insert(fragment_first, true);
    builder.index.metadata.addPath(1, 1, 0, 0);
    builder.insert(fragment_second, true);
    builder.index.metadata.addPath(1, 1, 0, 10);
    builder.index.metadata.addSamples(vector<string>{"sample1", "sample2"});
    builder.index.metadata.addContigs(vector<string>{"path1", "path2"});
    builder.finish();
    return make_unique<gbwt::GBWT>(builder.index);
}

static unique_ptr<MutablePathDeletableHandleGraph> shared_test_graph() {
    auto graph = make_unique<bdsg::PackedGraph>();
    const handle_t one = graph->create_handle("AAAA", 1);
    const handle_t two = graph->create_handle("CCC", 2);
    const handle_t three_reverse = graph->flip(graph->create_handle("GGGGG", 3));
    const handle_t four = graph->create_handle("TT", 4);
    graph->create_edge(one, two);
    graph->create_edge(two, three_reverse);
    graph->create_edge(three_reverse, two);
    graph->create_edge(two, four);
    return graph;
}

static vector<pair<string, vector<uint64_t>>> named_walks(const Transcriptome& transcriptome) {
    vector<pair<string, vector<uint64_t>>> walks;
    for (const auto& path : transcriptome.transcript_paths()) {
        vector<uint64_t> walk;
        path.for_each_handle(transcriptome.graph(), [&](const handle_t handle, uint64_t rank) {
            REQUIRE(rank == walk.size());
            walk.push_back(bdsg::as_integer(handle));
        });
        walks.emplace_back(path.get_name(), std::move(walk));
    }
    return walks;
}

static vector<gbwt::vector_type> transcript_gbwt_walks(const Transcriptome& transcriptome) {
    gbwt::Verbosity::set(gbwt::Verbosity::SILENT);
    gbwt::GBWTBuilder builder(gbwt::bit_length(gbwt::Node::encode(
        transcriptome.graph().max_node_id(), true)));
    transcriptome.add_transcripts_to_gbwt(&builder, true, false);
    builder.finish();
    vector<gbwt::vector_type> walks;
    for (gbwt::size_type i = 0; i < builder.index.sequences(); ++i) {
        walks.push_back(builder.index.extract(i));
    }
    return walks;
}

struct SharedRun {
    string graph;
    string info;
    string sequences;
    vector<pair<string, vector<uint64_t>>> walks;
    vector<gbwt::vector_type> gbwt_walks;
    vector<string> names;
};

struct SharedFixtureOptions {
    bool chop = false;
    bool write_sequences = false;
    bool add_to_gbwt = false;
    bool assert_unused_source = false;
    string second_annotation;
    bool stream_output = false;
    bool embedded_source_paths = false;
};

static SharedRun run_shared_fixture(const string& annotation, bool use_shared,
                                    const SharedFixtureOptions& options = {}) {
    auto source_graph = shared_test_graph();
    if (options.embedded_source_paths) {
        const auto reference = source_graph->create_path_handle("embedded_reference", false);
        source_graph->append_step(reference, source_graph->get_handle(1));
        source_graph->append_step(reference, source_graph->flip(source_graph->get_handle(3)));
        source_graph->append_step(reference, source_graph->get_handle(2));
        source_graph->append_step(reference, source_graph->flip(source_graph->get_handle(3)));
    }
    Transcriptome transcriptome(std::move(source_graph));
    transcriptome.num_threads = 1;
    transcriptome.path_collapse_type = "no";
    transcriptome.use_shared_reference_paths = use_shared;
    transcriptome.use_streaming_path_output = options.stream_output;

    stringstream annotation_stream(annotation);
    auto index = shared_test_index();
    transcriptome.add_reference_transcripts(vector<istream*>{&annotation_stream}, index, true, false);
    if (!options.second_annotation.empty()) {
        stringstream second_annotation_stream(options.second_annotation);
        transcriptome.add_reference_transcripts(vector<istream*>{&second_annotation_stream}, index, true, false);
    }
    if (use_shared && options.assert_unused_source) {
        REQUIRE(transcriptome.transcript_paths().size() == 1);
        const auto& selected = transcriptome.transcript_paths().front();
        REQUIRE(!selected.shared_path.empty());
        REQUIRE(selected.shared_path.size() == 1);
        REQUIRE(selected.shared_path.slices().size() == 1);
        REQUIRE(selected.shared_path.slices().front().source->size() > selected.shared_path.size());
    }
    if (options.chop) {
        transcriptome.chop_nodes(2);
    }
    transcriptome.remove_non_transcribed_nodes();
    if (options.embedded_source_paths) {
        // The original reference walk is removed before sorting; its deleted
        // PackedGraph slot remains for streamed output to preserve byte history.
        REQUIRE(transcriptome.graph().get_path_count() == 0);
    }
    REQUIRE(transcriptome.sort_compact_nodes());
    transcriptome.embed_transcript_paths(true, false);
    if (options.stream_output) {
        REQUIRE(transcriptome.graph().get_path_count() == 0);
        REQUIRE_FALSE(transcriptome.transcript_paths().empty());
        REQUIRE_THROWS(transcriptome.chop_nodes(2));
    }

    SharedRun result;
    result.walks = named_walks(transcriptome);
    for (const auto& path : transcriptome.transcript_paths()) {
        result.names.push_back(path.get_name());
    }
    ostringstream info;
    transcriptome.write_transcript_info(&info, *index, false);
    result.info = info.str();
    if (options.write_sequences) {
        ostringstream sequences;
        transcriptome.write_transcript_sequences(&sequences, false);
        result.sequences = sequences.str();
    }
    if (options.add_to_gbwt) {
        result.gbwt_walks = transcript_gbwt_walks(transcriptome);
    }
    ostringstream graph;
    transcriptome.write_graph(&graph);
    result.graph = graph.str();
    return result;
}

TEST_CASE("Shared edited transcript paths expand with graph orientation", "[transcriptome][shared_transcript_path]") {
    auto graph = make_unique<bdsg::PackedGraph>();
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
    REQUIRE(completed.get_last_node_handle(*graph) == one);
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

TEST_CASE("Shared GBWT reference paths match expanded no-collapse control", "[transcriptome][shared_transcript_path]") {
    // Coordinates are GFF one-based and inclusive. tx_same_end_a/b are distinct
    // names with identical endpoints; tx_cross overlaps/crosses and is excluded.
    const string annotation =
        "sample1#0#path1\t.\texon\t1\t4\t.\t+\t.\ttranscript_id \"tx_whole\";\n"
        "sample1#0#path1\t.\texon\t5\t5\t.\t+\t.\ttranscript_id \"tx_two_in_node\";\n"
        "sample1#0#path1\t.\texon\t7\t7\t.\t+\t.\ttranscript_id \"tx_two_in_node\";\n"
        "sample1#0#path1\t.\texon\t2\t3\t.\t+\t.\ttranscript_id \"tx_partial\";\n"
        "sample1#0#path1\t.\texon\t9\t11\t.\t+\t.\ttranscript_id \"tx_partial\";\n"
        "sample1#0#path1\t.\texon\t2\t3\t.\t-\t.\ttranscript_id \"tx_reverse\";\n"
        "sample1#0#path1\t.\texon\t9\t11\t.\t-\t.\ttranscript_id \"tx_reverse\";\n"
        "sample1#0#path1\t.\texon\t5\t7\t.\t+\t.\ttranscript_id \"tx_fragment\";\n"
        "sample1#0#path1\t.\texon\t13\t15\t.\t+\t.\ttranscript_id \"tx_fragment\";\n"
        "sample2#0#path2\t.\texon\t5\t7\t.\t+\t.\ttranscript_id \"tx_fragment_gap\";\n"
        "sample2#0#path2\t.\texon\t11\t15\t.\t+\t.\ttranscript_id \"tx_fragment_gap\";\n"
        "sample2#0#path2\t.\texon\t6\t12\t.\t+\t.\ttranscript_id \"tx_cross_fragment\";\n"
        "sample1#0#path1\t.\texon\t5\t7\t.\t+\t.\ttranscript_id \"tx_same_end_a\";\n"
        "sample1#0#path1\t.\texon\t5\t7\t.\t+\t.\ttranscript_id \"tx_same_end_b\";\n"
        "sample1#0#path1\t.\texon\t6\t8\t.\t+\t.\ttranscript_id \"tx_cross\";\n"
        "sample1#0#path1\t.\texon\t5\t7\t.\t+\t.\ttranscript_id \"tx_cross\";\n";

    const SharedRun expanded = run_shared_fixture(annotation, false);
    const SharedRun shared = run_shared_fixture(annotation, true);

    REQUIRE(shared.graph == expanded.graph);
    REQUIRE(shared.info == expanded.info);
    REQUIRE(shared.walks == expanded.walks);
    REQUIRE(shared.names == expanded.names);
    REQUIRE(find(shared.names.begin(), shared.names.end(), "tx_same_end_a_R1") != shared.names.end());
    REQUIRE(find(shared.names.begin(), shared.names.end(), "tx_same_end_b_R1") != shared.names.end());
    REQUIRE(find(shared.names.begin(), shared.names.end(), "tx_reverse_R1") != shared.names.end());
    REQUIRE(find(shared.names.begin(), shared.names.end(), "tx_cross_R1") == shared.names.end());
    REQUIRE(find(shared.names.begin(), shared.names.end(), "tx_fragment_gap_R1") != shared.names.end());
    REQUIRE(find(shared.names.begin(), shared.names.end(), "tx_cross_fragment_R1") == shared.names.end());
}

TEST_CASE("Shared GBWT whole-node route matches expanded without augmentation", "[transcriptome][shared_transcript_path]") {
    const string annotation =
        "sample1#0#path1\t.\texon\t1\t4\t.\t+\t.\ttranscript_id \"tx_whole_a\";\n"
        "sample1#0#path1\t.\texon\t5\t7\t.\t+\t.\ttranscript_id \"tx_whole_b\";\n"
        "sample1#0#path1\t.\texon\t13\t15\t.\t+\t.\ttranscript_id \"tx_whole_b\";\n";
    const SharedRun expanded = run_shared_fixture(annotation, false);
    const SharedRun shared = run_shared_fixture(annotation, true);
    REQUIRE(shared.graph == expanded.graph);
    REQUIRE(shared.info == expanded.info);
    REQUIRE(shared.walks == expanded.walks);
}

TEST_CASE("Shared GBWT paths survive chopping, sequence and GBWT consumers", "[transcriptome][shared_transcript_path]") {
    const string annotation =
        "sample1#0#path1\t.\texon\t1\t4\t.\t+\t.\ttranscript_id \"tx_repeat\";\n"
        "sample1#0#path1\t.\texon\t5\t7\t.\t+\t.\ttranscript_id \"tx_repeat\";\n"
        "sample1#0#path1\t.\texon\t8\t12\t.\t+\t.\ttranscript_id \"tx_repeat\";\n"
        "sample1#0#path1\t.\texon\t13\t15\t.\t+\t.\ttranscript_id \"tx_repeat\";\n"
        "sample1#0#path1\t.\texon\t2\t3\t.\t-\t.\ttranscript_id \"tx_reverse_cut\";\n"
        "sample1#0#path1\t.\texon\t9\t11\t.\t-\t.\ttranscript_id \"tx_reverse_cut\";\n";
    const SharedFixtureOptions options{true, true, true};
    const SharedRun expanded = run_shared_fixture(annotation, false, options);
    const SharedRun shared = run_shared_fixture(annotation, true, options);
    REQUIRE(shared.graph == expanded.graph);
    REQUIRE(shared.info == expanded.info);
    REQUIRE(shared.walks == expanded.walks);
    REQUIRE(shared.sequences == expanded.sequences);
    REQUIRE(shared.gbwt_walks == expanded.gbwt_walks);
}

TEST_CASE("Shared source slices tolerate removing source-only nodes before sorting", "[transcriptome][shared_transcript_path]") {
    // The rejected exon crosses sample2's fragment gap. Its first-fragment
    // overlap retains node 2 in the shared source, but only tx_valid remains.
    const string annotation =
        "sample2#0#path2\t.\texon\t1\t4\t.\t+\t.\ttranscript_id \"tx_valid\";\n"
        "sample2#0#path2\t.\texon\t6\t12\t.\t+\t.\ttranscript_id \"tx_rejected_cross_fragment\";\n";
    const SharedRun expanded = run_shared_fixture(annotation, false);
    SharedFixtureOptions shared_options;
    shared_options.assert_unused_source = true;
    const SharedRun shared = run_shared_fixture(annotation, true, shared_options);
    REQUIRE(shared.graph == expanded.graph);
    REQUIRE(shared.info == expanded.info);
    REQUIRE(shared.walks == expanded.walks);
    REQUIRE(shared.walks.size() == 1);
    REQUIRE(shared.walks.front().first == "tx_valid_R1");
    REQUIRE(shared.walks.front().second.size() == 1);
}

TEST_CASE("Shared GBWT paths match after a second reference augmentation", "[transcriptome][shared_transcript_path]") {
    const string first_annotation =
        "sample1#0#path1\t.\texon\t1\t4\t.\t+\t.\ttranscript_id \"tx_first_whole\";\n";
    SharedFixtureOptions options;
    options.second_annotation =
        "sample1#0#path1\t.\texon\t2\t3\t.\t+\t.\ttranscript_id \"tx_second_partial\";\n";
    const SharedRun expanded = run_shared_fixture(first_annotation, false, options);
    const SharedRun shared = run_shared_fixture(first_annotation, true, options);
    REQUIRE(shared.graph == expanded.graph);
    REQUIRE(shared.info == expanded.info);
    REQUIRE(shared.walks == expanded.walks);
    REQUIRE(shared.names == expanded.names);
}

TEST_CASE("Shared RNA streamed output matches mutable path embedding", "[transcriptome][shared_transcript_path][stream]") {
    const string annotation =
        "sample1#0#path1\t.\texon\t1\t4\t.\t+\t.\ttranscript_id \"tx_whole\";\n"
        "sample1#0#path1\t.\texon\t5\t7\t.\t+\t.\ttranscript_id \"tx_repeat\";\n"
        "sample1#0#path1\t.\texon\t13\t15\t.\t+\t.\ttranscript_id \"tx_repeat\";\n"
        "sample1#0#path1\t.\texon\t2\t3\t.\t-\t.\ttranscript_id \"tx_reverse\";\n"
        "sample1#0#path1\t.\texon\t9\t11\t.\t-\t.\ttranscript_id \"tx_reverse\";\n";
    SharedFixtureOptions options{true, true, true};
    options.embedded_source_paths = true;
    const auto expanded = run_shared_fixture(annotation, false, options);
    options.stream_output = true;
    const auto streamed = run_shared_fixture(annotation, true, options);
    REQUIRE(streamed.graph == expanded.graph);
    REQUIRE(streamed.info == expanded.info);
    REQUIRE(streamed.walks == expanded.walks);
    REQUIRE(streamed.sequences == expanded.sequences);
    REQUIRE(streamed.gbwt_walks == expanded.gbwt_walks);
}

TEST_CASE("Parallel splice discovery preserves first insertion and bounded fallback", "[transcriptome][parallel_splices]") {
    auto run = [](int threads) {
        auto graph = make_unique<bdsg::PackedGraph>();
        gbwt::GBWTBuilder builder(gbwt::bit_length(gbwt::Node::encode(260, true)));
        builder.index.addMetadata();
        gbwt::vector_type walk;
        for (nid_t id = 1; id <= 260; ++id) {
            graph->create_handle("A", id);
            if (id > 1) { graph->create_edge(graph->get_handle(id - 1), graph->get_handle(id)); }
            walk.push_back(gbwt::Node::encode(id, false));
        }
        builder.insert(walk, true);
        builder.index.metadata.addPath(0, 0, 0, 0);
        builder.index.metadata.addSamples(vector<string>{"sample"});
        builder.index.metadata.addContigs(vector<string>{"chr"});
        builder.finish();
        auto index = make_unique<gbwt::GBWT>(builder.index);
        stringstream annotation;
        // Every long path proposes 129 absent edges, exceeding the 128-edge
        // worker buffer. Short paths mix ordinary discovery with that fallback.
        for (size_t tx = 0; tx < 32; ++tx) {
            const size_t end = tx % 2 == 0 ? 259 : 127;
            for (size_t pos = 1; pos <= end; pos += 2) {
                annotation << "sample#0#chr\t.\texon\t" << pos << '\t' << pos
                           << "\t.\t+\t.\ttranscript_id \"tx" << tx << "\";\n";
            }
        }
        Transcriptome transcriptome(std::move(graph));
        transcriptome.num_threads = threads;
        transcriptome.path_collapse_type = "no";
        transcriptome.add_reference_transcripts(vector<istream*>{&annotation}, index, true, false);
        REQUIRE(transcriptome.transcript_paths().size() == 32);
        transcriptome.remove_non_transcribed_nodes();
        REQUIRE(transcriptome.graph().get_node_count() == 130);
        REQUIRE(transcriptome.graph().get_edge_count() == 129);
        REQUIRE(transcriptome.sort_compact_nodes());
        transcriptome.embed_transcript_paths(true, false);
        ostringstream out;
        transcriptome.write_graph(&out);
        return out.str();
    };
    const auto expected = run(1);
    for (const int threads : {2, 4, 24}) { REQUIRE(run(threads) == expected); }
}

TEST_CASE("Parallel transcribed node collection preserves graph mutation order", "[transcriptome][parallel_nodes]") {
    for (const bool sparse_ids : {false, true}) {
        auto run = [&](int threads, bool malformed) {
            auto graph = make_unique<bdsg::PackedGraph>();
            vector<handle_t> handles;
            for (size_t i = 0; i < 150; ++i) {
                const nid_t id = sparse_ids ? 1 + i * 1009 : 1 + 2 * i;
                handles.push_back(graph->create_handle("AC", id));
                if (i != 0) { graph->create_edge(handles[i - 1], handles[i]); }
            }
            const auto embedded = graph->create_path_handle("original");
            for (const auto& handle : handles) { graph->append_step(embedded, handle); }
            vector<EditedMapping> mappings;
            for (size_t i = 0; i < 190; ++i) {
                const auto handle = handles[i % 129];
                mappings.push_back({i % 3 == 0 ? graph->flip(handle) : handle, 0, 2});
            }
            auto source = make_shared<SharedTranscriptPath<EditedMapping>::Source>(mappings);
            Transcriptome transcriptome(std::move(graph));
            transcriptome.num_threads = threads;
            auto& paths = const_cast<vector<CompletedTranscriptPath>&>(transcriptome.transcript_paths());
            for (size_t i = 0; i < 257; ++i) {
                EditedTranscriptPath edited("tx" + to_string(i), "source", true, false);
                if (i % 2 == 0) {
                    edited.shared_path.append(source, 0, mappings.size(), 0, 2);
                } else {
                    edited.path = mappings;
                }
                paths.emplace_back(edited, transcriptome.graph());
            }
            if (malformed) {
                // Neither dense nor sparse rank tables may silently mark a
                // missing node; the exception must escape the worker region.
                paths[1].path.push_back(transcriptome.graph().get_handle(2));
                REQUIRE_THROWS_AS(transcriptome.remove_non_transcribed_nodes(), invalid_argument);
                REQUIRE(transcriptome.graph().get_node_count() == 150);
                paths[1].path.pop_back();
            }
            transcriptome.remove_non_transcribed_nodes();
            REQUIRE(transcriptome.graph().get_node_count() == 129);
            REQUIRE(transcriptome.graph().get_path_count() == 0);
            ostringstream out;
            transcriptome.write_graph(&out);
            return out.str();
        };
        const string expected = run(1, false);
        for (const int threads : {2, 4, 24}) {
            REQUIRE(run(threads, false) == expected);
            REQUIRE(run(threads, true) == expected);
        }
    }
}

TEST_CASE("Transcript info parallel batches retain rows and propagate failures", "[transcriptome][parallel_info]") {
    Transcriptome transcriptome(shared_test_graph());
    transcriptome.num_threads = 1;
    transcriptome.path_collapse_type = "no";
    auto index = shared_test_index();
    stringstream annotation("sample1#0#path1\t.\texon\t1\t15\t.\t+\t.\ttranscript_id \"tx\";\n");
    transcriptome.add_reference_transcripts(vector<istream*>{&annotation}, index, true, false);
    REQUIRE_FALSE(transcriptome.transcript_paths().empty());

    // The graph/walk is a real constructed fixture. Expand only its metadata
    // population here to cross multiple output batches without rebuilding it.
    auto& paths = const_cast<vector<CompletedTranscriptPath>&>(transcriptome.transcript_paths());
    const CompletedTranscriptPath prototype = paths.front();
    paths.assign(8201, prototype);
    for (size_t i = 0; i < paths.size(); ++i) {
        paths[i].transcript_names = {"row_" + to_string(i)};
        paths[i].copy_id = 1;
        paths[i].is_haplotype = i % 3 == 0;
        paths[i].haplotype_gbwt_ids = {{0, true}, {0, true}, {2, false}, {3, false}};
        paths[i].embedded_path_names = {{"embedded", true}, {"embedded", true}};
    }
    // Exercise bounded-buffer and metadata fallbacks among ordinary rows.
    paths[0].transcript_names.assign(33, string(4096, 'a'));
    paths[1].embedded_path_names.assign(129, make_pair(string("many"), false));
    paths[2].transcript_names = {string(4097, 'b')};

    auto info = [&](int threads, bool exclude_reference, bool hex_format) {
        transcriptome.num_threads = threads;
        ostringstream out;
        if (hex_format) { out << hex; }
        transcriptome.write_transcript_info(&out, *index, exclude_reference);
        return out.str();
    };
    for (const bool exclude : {false, true}) {
        for (const bool hex_format : {false, true}) {
            const string expected = info(1, exclude, hex_format);
            REQUIRE(expected.find("Name\tLength\tTranscripts\tHaplotypes\n") == 0);
            for (const int threads : {2, 4, 24}) {
                REQUIRE(info(threads, exclude, hex_format) == expected);
            }
        }
    }

    SECTION("worker exceptions reach the caller after joining") {
        paths[17].path_is_spooled = true;
        paths[17].path_length = numeric_limits<uint64_t>::max();
        transcriptome.num_threads = 4;
        ostringstream out;
        REQUIRE_THROWS_AS(transcriptome.write_transcript_info(&out, *index, false), overflow_error);
        paths[17].path_is_spooled = false;
        paths[17].path_length = 0;
        REQUIRE(info(4, false, false) == info(1, false, false));
    }
    SECTION("output errors reach the caller after joining") {
        class FailingBuffer : public streambuf {
        public:
            size_t remaining = 64;
        protected:
            streamsize xsputn(const char*, streamsize count) override {
                const size_t written = min(remaining, static_cast<size_t>(count));
                remaining -= written;
                return static_cast<streamsize>(written);
            }
            int_type overflow(int_type) override { return traits_type::eof(); }
        } buffer;
        ostream out(&buffer);
        transcriptome.num_threads = 4;
        REQUIRE_THROWS_AS(transcriptome.write_transcript_info(&out, *index, false), runtime_error);
    }
}

} // namespace unittest
} // namespace vg
