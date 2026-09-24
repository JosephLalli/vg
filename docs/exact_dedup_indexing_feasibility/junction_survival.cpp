// Splice-junction survival through vg prune.
//
// A splice junction is an edge that some spliced transcript path traverses and
// no transcript-body path traverses. Bodies are the unspliced walks of the same
// transcripts on the same haplotypes, so they share every within-exon edge and
// cross every intron through genomic sequence; only the junction edges are left.
//
// The pruned graph keeps surviving original nodes under their own IDs and adds
// unfolded duplicates at IDs >= the mapping's first_node. Each pruned edge is
// mapped back to original node IDs through the prune node mapping (gcsa
// NodeMapping: uint64 first_node, uint64 next_node, then next - first uint64
// original IDs), and a junction survives if any pruned edge maps onto it in
// either orientation.
//
// usage: junction_survival GENIC.pg BODY_PATH_NAMES.txt PRUNED.pg MAPPING THREADS

#include <bdsg/packed_graph.hpp>

#include <omp.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

using namespace std;
using handlegraph::handle_t;
using handlegraph::path_handle_t;
using handlegraph::step_handle_t;

namespace {

double since(chrono::steady_clock::time_point t0) {
    return chrono::duration<double>(chrono::steady_clock::now() - t0).count();
}

inline uint64_t side(uint64_t id, bool rev) { return (id << 1) | (rev ? 1 : 0); }
inline uint64_t flip(uint64_t s) { return s ^ 1; }

// The same edge read from either strand yields the same key.
inline uint64_t edge_key(uint64_t a, uint64_t b) {
    uint64_t fwd = (a << 32) | b;
    uint64_t rev = (flip(b) << 32) | flip(a);
    return min(fwd, rev);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 6) {
        cerr << "usage: " << argv[0] << " GENIC.pg BODY_PATH_NAMES.txt PRUNED.pg MAPPING THREADS" << endl;
        return 2;
    }
    const string genic_file = argv[1], body_file = argv[2], pruned_file = argv[3], mapping_file = argv[4];
    const int threads = stoi(argv[5]);
    omp_set_num_threads(threads);
    auto t0 = chrono::steady_clock::now();

    unordered_set<string> body_names;
    {
        ifstream in(body_file);
        string line;
        while (getline(in, line)) {
            if (!line.empty()) body_names.insert(line);
        }
    }
    cerr << "[" << since(t0) << " s] body path names: " << body_names.size() << endl;

    auto genic_owner = make_unique<bdsg::PackedGraph>();
    genic_owner->deserialize(genic_file);
    const bdsg::PackedGraph& genic = *genic_owner;
    const uint64_t genic_max_id = genic.max_node_id();
    cerr << "[" << since(t0) << " s] genic graph: " << genic.get_node_count() << " nodes, "
         << genic.get_edge_count() << " edges, " << genic.get_path_count() << " paths" << endl;
    if ((genic_max_id << 1) >= (1ull << 32)) {
        cerr << "node IDs too large for 32-bit sides" << endl;
        return 1;
    }

    vector<path_handle_t> body_paths, spliced_paths;
    genic.for_each_path_handle([&](const path_handle_t& p) {
        if (body_names.count(genic.get_path_name(p))) body_paths.push_back(p);
        else spliced_paths.push_back(p);
    });
    const size_t body_listed = body_names.size();
    body_names.clear();
    cerr << "[" << since(t0) << " s] body paths " << body_paths.size() << " of " << body_listed
         << " listed; spliced paths " << spliced_paths.size() << endl;

    auto collect = [&](const vector<path_handle_t>& paths, unordered_set<uint64_t>& out, uint64_t& steps_out) {
        vector<unordered_set<uint64_t>> local(threads);
        vector<uint64_t> steps(threads, 0);
#pragma omp parallel for schedule(dynamic, 256)
        for (size_t i = 0; i < paths.size(); ++i) {
            auto& mine = local[omp_get_thread_num()];
            uint64_t prev = UINT64_MAX;
            uint64_t n = 0;
            genic.for_each_step_in_path(paths[i], [&](const step_handle_t& s) {
                handle_t h = genic.get_handle_of_step(s);
                uint64_t cur = side(genic.get_id(h), genic.get_is_reverse(h));
                if (prev != UINT64_MAX) mine.insert(edge_key(prev, cur));
                prev = cur;
                ++n;
            });
            steps[omp_get_thread_num()] += n;
        }
        steps_out = 0;
        for (int t = 0; t < threads; ++t) {
            out.insert(local[t].begin(), local[t].end());
            steps_out += steps[t];
        }
    };

    unordered_set<uint64_t> spliced_edges, body_edges;
    uint64_t spliced_steps = 0, body_steps = 0;
    collect(spliced_paths, spliced_edges, spliced_steps);
    cerr << "[" << since(t0) << " s] spliced: " << spliced_steps << " steps, " << spliced_edges.size() << " distinct edges" << endl;
    collect(body_paths, body_edges, body_steps);
    cerr << "[" << since(t0) << " s] body: " << body_steps << " steps, " << body_edges.size() << " distinct edges" << endl;

    vector<uint64_t> junctions;
    for (uint64_t e : spliced_edges) {
        if (!body_edges.count(e)) junctions.push_back(e);
    }
    sort(junctions.begin(), junctions.end());
    uint64_t junctions_in_graph = 0;
    for (uint64_t e : junctions) {
        uint64_t a = e >> 32, b = e & 0xffffffffull;
        handle_t ha = genic.get_handle(a >> 1, a & 1), hb = genic.get_handle(b >> 1, b & 1);
        if (genic.has_edge(ha, hb)) ++junctions_in_graph;
    }
    cerr << "[" << since(t0) << " s] junction edges: " << junctions.size()
         << " (" << junctions_in_graph << " present as graph edges)" << endl;

    // Free the genic graph before loading the pruned one.
    unordered_set<uint64_t>().swap(body_edges);
    genic_owner.reset();

    uint64_t first_node = 0, next_node = 0;
    vector<uint64_t> mapping;
    {
        ifstream in(mapping_file, ios::binary);
        in.read(reinterpret_cast<char*>(&first_node), sizeof(first_node));
        in.read(reinterpret_cast<char*>(&next_node), sizeof(next_node));
        mapping.resize(next_node - first_node);
        in.read(reinterpret_cast<char*>(mapping.data()), mapping.size() * sizeof(uint64_t));
        if (!in) { cerr << "short mapping file" << endl; return 1; }
    }
    cerr << "[" << since(t0) << " s] mapping: first_node " << first_node << ", " << mapping.size() << " duplicates" << endl;

    bdsg::PackedGraph pruned;
    pruned.deserialize(pruned_file);
    cerr << "[" << since(t0) << " s] pruned graph: " << pruned.get_node_count() << " nodes, "
         << pruned.get_edge_count() << " edges" << endl;

    auto original = [&](uint64_t id) -> uint64_t {
        return (id >= first_node && id < next_node) ? mapping[id - first_node] : id;
    };
    unordered_set<uint64_t> pruned_edges_orig;
    uint64_t pruned_edges_total = 0, unmappable = 0;
    pruned.for_each_edge([&](const handlegraph::edge_t& e) {
        ++pruned_edges_total;
        uint64_t ia = original(pruned.get_id(e.first)), ib = original(pruned.get_id(e.second));
        if (ia > genic_max_id || ib > genic_max_id) { ++unmappable; return; }
        pruned_edges_orig.insert(edge_key(side(ia, pruned.get_is_reverse(e.first)),
                                          side(ib, pruned.get_is_reverse(e.second))));
    });
    cerr << "[" << since(t0) << " s] pruned edges " << pruned_edges_total << ", distinct after mapping "
         << pruned_edges_orig.size() << ", unmappable " << unmappable << endl;

    uint64_t survived = 0;
    vector<uint64_t> missing;
    for (uint64_t e : junctions) {
        if (pruned_edges_orig.count(e)) ++survived;
        else missing.push_back(e);
    }

    cout << "genic_nodes_max_id\t" << genic_max_id << "\n"
         << "spliced_paths\t" << spliced_paths.size() << "\n"
         << "body_paths\t" << body_paths.size() << "\n"
         << "spliced_steps\t" << spliced_steps << "\n"
         << "body_steps\t" << body_steps << "\n"
         << "spliced_distinct_edges\t" << spliced_edges.size() << "\n"
         << "junction_edges\t" << junctions.size() << "\n"
         << "junction_edges_present_in_genic_graph\t" << junctions_in_graph << "\n"
         << "pruned_edges\t" << pruned_edges_total << "\n"
         << "pruned_edges_distinct_after_mapping\t" << pruned_edges_orig.size() << "\n"
         << "pruned_edges_unmappable\t" << unmappable << "\n"
         << "junction_edges_surviving\t" << survived << "\n"
         << "junction_edges_missing\t" << missing.size() << "\n";
    for (size_t i = 0; i < missing.size() && i < 25; ++i) {
        uint64_t a = missing[i] >> 32, b = missing[i] & 0xffffffffull;
        cout << "missing_example\t" << (a >> 1) << (a & 1 ? "-" : "+") << "\t" << (b >> 1) << (b & 1 ? "-" : "+") << "\n";
    }
    cerr << "[" << since(t0) << " s] done" << endl;
    return 0;
}
