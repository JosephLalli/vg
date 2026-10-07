#include "transcriptome.hpp"
#include "bdsg/packed_graph.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
using namespace vg;
using namespace std;
int main(int argc, char** argv) {
    if (argc != 2) return 1;
    auto graph = make_unique<bdsg::PackedGraph>();
    vector<handle_t> handles;
    for (size_t i = 0; i < 512; ++i) handles.push_back(graph->create_handle("ACGT"));
    for (size_t i = 1; i < handles.size(); ++i) graph->create_edge(handles[i - 1], handles[i]);
    Transcriptome transcriptome(std::move(graph));
    auto& paths = const_cast<vector<CompletedTranscriptPath>&>(transcriptome.transcript_paths());
    constexpr size_t path_count = 100, steps = 20000;
    vector<EditedMapping> mappings;
    for (size_t rank = 0; rank < steps; ++rank) {
        auto handle = handles[rank % handles.size()];
        if ((rank / handles.size()) % 2) handle = transcriptome.graph().flip(handle);
        mappings.push_back({handle, 0, 4});
    }
    auto source = make_shared<SharedTranscriptPath<EditedMapping>::Source>(std::move(mappings));
    for (size_t i = 0; i < path_count; ++i) {
        EditedTranscriptPath edited("transcript_" + to_string(i), "source", true, false);
        edited.shared_path.append(source, 0, steps, 0, 4);
        paths.emplace_back(edited, transcriptome.graph());
    }
    transcriptome.num_threads = 4;
    auto start = chrono::steady_clock::now();
    ofstream output(argv[1], ios::binary);
    transcriptome.write_graph_with_transcript_paths(&output, true, false);
    output.close();
    if (!output) return 2;
    cout << chrono::duration<double>(chrono::steady_clock::now() - start).count() << '\n';
}
