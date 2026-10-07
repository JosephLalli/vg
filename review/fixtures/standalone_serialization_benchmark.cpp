#include "transcriptome.hpp"
#include "bdsg/packed_graph.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace vg;
using namespace std;

static void append_complete_step(EditedTranscriptPath& transcript_path,
                                 const HandleGraph& graph,
                                 const handle_t& handle) {
    auto* mapping = transcript_path.path.add_mapping();
    mapping->mutable_position()->set_node_id(graph.get_id(handle));
    mapping->mutable_position()->set_is_reverse(graph.get_is_reverse(handle));
    auto* edit = mapping->add_edit();
    edit->set_from_length(graph.get_length(handle));
    edit->set_to_length(graph.get_length(handle));
}

int main(int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }

    auto graph = make_unique<bdsg::PackedGraph>();
    vector<handle_t> handles;
    handles.reserve(512);
    for (size_t i = 0; i < 512; ++i) {
        handles.push_back(graph->create_handle("ACGT"));
    }
    for (size_t i = 1; i < handles.size(); ++i) {
        graph->create_edge(handles[i - 1], handles[i]);
    }

    Transcriptome transcriptome(std::move(graph));
    auto& paths = const_cast<vector<CompletedTranscriptPath>&>(transcriptome.transcript_paths());
    constexpr size_t path_count = 100;
    constexpr size_t steps = 20000;
    for (size_t i = 0; i < path_count; ++i) {
        EditedTranscriptPath edited("transcript_" + to_string(i), "source", true, false);
        for (size_t rank = 0; rank < steps; ++rank) {
            handle_t handle = handles[rank % handles.size()];
            if ((rank / handles.size()) % 2 != 0) {
                handle = transcriptome.graph().flip(handle);
            }
            append_complete_step(edited, transcriptome.graph(), handle);
        }
        paths.emplace_back(edited, transcriptome.graph());
    }

    transcriptome.num_threads = 4;
    const auto start = chrono::steady_clock::now();
    ofstream output(argv[1], ios::binary);
    transcriptome.write_graph_with_transcript_paths(&output, true, false);
    output.close();
    if (!output) {
        return 2;
    }
    cout << chrono::duration<double>(chrono::steady_clock::now() - start).count() << '\n';
}
