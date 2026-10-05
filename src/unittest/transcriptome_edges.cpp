/// \file transcriptome_edges.cpp
///
/// Unit tests for adding transcript edges.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#include "bdsg/packed_graph.hpp"
#include "gbwt/dynamic_gbwt.h"

#include "../transcriptome.hpp"
#include "catch.hpp"

namespace vg {
namespace unittest {

using namespace std;

namespace {

using EncodedEdge = pair<uint64_t, uint64_t>;

vector<EncodedEdge> encoded_edges(const HandleGraph& graph) {

    vector<EncodedEdge> result;
    graph.for_each_edge([&](const edge_t& edge) {
        result.emplace_back(bdsg::as_integer(edge.first), bdsg::as_integer(edge.second));
        return true;
    });
    sort(result.begin(), result.end());
    return result;
}

}

TEST_CASE("Parallel transcript edge discovery preserves oriented splice edges",
          "[transcriptome]") {

    struct Result {
        vector<EncodedEdge> edges;
        bool has_self_edge;
        bool has_reversing_self_edge;
        bool has_reverse_edge;
        bool has_preexisting_edge;
    };

    auto run = [&](int32_t threads) {
        unique_ptr<MutablePathDeletableHandleGraph> graph(new bdsg::PackedGraph());
        auto a = graph->create_handle("A");
        auto b = graph->create_handle("C");
        auto c = graph->create_handle("G");
        auto d = graph->create_handle("T");
        auto e = graph->create_handle("A");
        auto f = graph->create_handle("C");
        auto g = graph->create_handle("G");

        vector<handle_t> steps{a, b, a, c, graph->flip(a), d, e, f, g};
        auto path = graph->create_path_handle("ref");
        for (size_t i = 0; i < steps.size(); ++i) {
            graph->append_step(path, steps[i]);
            if (i != 0) {
                graph->create_edge(steps[i - 1], steps[i]);
            }
        }

        // The final intron requests this edge again.
        graph->create_edge(e, g);

        Transcriptome transcriptome(std::move(graph));
        transcriptome.num_threads = threads;
        unique_ptr<gbwt::GBWT> haplotypes(new gbwt::GBWT());
        stringstream introns;
        introns << "ref\t1\t2\t.\t0\t+\n"; // a -> a
        introns << "ref\t1\t2\t.\t0\t+\n"; // duplicate
        introns << "ref\t3\t4\t.\t0\t+\n"; // a -> flip(a)
        introns << "ref\t5\t6\t.\t0\t-\n"; // flip(e) -> a
        introns << "ref\t7\t8\t.\t0\t+\n"; // pre-existing e -> g

        REQUIRE(transcriptome.add_intron_splice_junctions(
            vector<istream*>{&introns}, haplotypes, false) == 5);

        const auto& result_graph = transcriptome.graph();
        return Result{
            encoded_edges(result_graph),
            result_graph.has_edge(a, a),
            result_graph.has_edge(a, result_graph.flip(a)),
            result_graph.has_edge(result_graph.flip(e), a),
            result_graph.has_edge(e, g)
        };
    };

    auto serial = run(1);
    auto parallel = run(4);
    REQUIRE(parallel.edges == serial.edges);
    REQUIRE(parallel.edges.size() == 12);
    REQUIRE(parallel.has_self_edge);
    REQUIRE(parallel.has_reversing_self_edge);
    REQUIRE(parallel.has_reverse_edge);
    REQUIRE(parallel.has_preexisting_edge);
}

TEST_CASE("Parallel transcript edge discovery falls back for long paths",
          "[transcriptome]") {

    auto run = [&](int32_t threads) {
        unique_ptr<MutablePathDeletableHandleGraph> graph(new bdsg::PackedGraph());
        vector<handle_t> handles;
        handles.reserve(263);
        auto path = graph->create_path_handle("ref");
        for (size_t i = 0; i < 263; ++i) {
            handles.push_back(graph->create_handle("A"));
            graph->append_step(path, handles.back());
            if (i != 0) {
                graph->create_edge(handles[i - 1], handles[i]);
            }
        }

        // The long transcript has 130 splice edges and 129 are missing.
        graph->create_edge(handles[10], handles[12]);

        Transcriptome transcriptome(std::move(graph));
        transcriptome.num_threads = threads;
        unique_ptr<gbwt::GBWT> haplotypes(new gbwt::GBWT());
        stringstream transcripts;
        for (size_t i = 0; i < 131; ++i) {
            size_t position = 2 * i + 1;
            transcripts << "ref\t.\texon\t" << position << "\t" << position
                        << "\t.\t+\t.\ttranscript_id \"long\";\n";
        }
        for (size_t position : {1, 3, 5}) {
            transcripts << "ref\t.\texon\t" << position << "\t" << position
                        << "\t.\t+\t.\ttranscript_id \"short\";\n";
        }

        REQUIRE(transcriptome.add_reference_transcripts(
            vector<istream*>{&transcripts}, haplotypes, false, false) == 2);
        REQUIRE(transcriptome.reference_transcript_paths().size() == 2);
        REQUIRE(transcriptome.graph().has_edge(handles[258], handles[260]));
        return encoded_edges(transcriptome.graph());
    };

    auto serial = run(1);
    auto parallel = run(4);
    REQUIRE(parallel == serial);
    REQUIRE(parallel.size() == 392);
}

}
}
