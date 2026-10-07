#include "integrated_snarl_finder.hpp"
#include "snarl_distance_index.hpp"

#include <bdsg/hash_graph.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using bdsg::HashGraph;
using bdsg::SnarlDistanceIndex;
using vg::IntegratedSnarlFinder;
using vg::fill_in_distance_index;

static void save(const SnarlDistanceIndex& index, const std::string& filename) {
    int fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        throw std::runtime_error("cannot open output");
    }
    index.serialize(fd);
    if (close(fd) != 0) {
        throw std::runtime_error("cannot close output");
    }
}

static void dump_and_check_queries(const SnarlDistanceIndex& index, const HashGraph& graph,
                                   const std::vector<int64_t>& ids, const std::string& filename) {
    SnarlDistanceIndex loaded;
    loaded.deserialize(filename);
    std::ofstream out(filename + ".queries.tsv");
    if (!out) {
        throw std::runtime_error("cannot open query output");
    }
    for (int64_t id1 : ids) {
        size_t length1 = graph.get_length(graph.get_handle(id1));
        for (bool rev1 : {false, true}) {
            for (size_t offset1 = 0; offset1 < length1; ++offset1) {
                for (int64_t id2 : ids) {
                    size_t length2 = graph.get_length(graph.get_handle(id2));
                    for (bool rev2 : {false, true}) {
                        for (size_t offset2 = 0; offset2 < length2; ++offset2) {
                            for (bool unoriented : {false, true}) {
                                size_t before = index.minimum_distance(id1, rev1, offset1, id2, rev2,
                                                                       offset2, unoriented, &graph);
                                size_t after = loaded.minimum_distance(id1, rev1, offset1, id2, rev2,
                                                                       offset2, unoriented, &graph);
                                if (before != after) {
                                    throw std::runtime_error("serialized query mismatch");
                                }
                                out << id1 << '\t' << rev1 << '\t' << offset1 << '\t'
                                    << id2 << '\t' << rev2 << '\t' << offset2 << '\t'
                                    << unoriented << '\t' << before << '\n';
                            }
                        }
                    }
                }
            }
        }
    }
}

static HashGraph make_bubble() {
    HashGraph graph;
    auto n1 = graph.create_handle("GCA", 1);
    auto n2 = graph.create_handle("T", 2);
    auto n3 = graph.create_handle("G", 3);
    auto n4 = graph.create_handle("CTGA", 4);
    graph.create_edge(n1, n2);
    graph.create_edge(n1, n3);
    graph.create_edge(n2, n3);
    graph.create_edge(n2, n4);
    graph.create_edge(n3, n4);
    return graph;
}

static void check_bubble(const std::string& prefix, size_t limit, const std::string& label) {
    HashGraph graph = make_bubble();
    IntegratedSnarlFinder finder(graph);
    SnarlDistanceIndex index;
    fill_in_distance_index(&index, &graph, &finder, limit);
    if (index.minimum_distance(2, false, 0, 3, false, 0, false, &graph) != 1) {
        throw std::runtime_error(label + " query mismatch");
    }
    std::string filename = prefix + "." + label + ".dist";
    save(index, filename);
    dump_and_check_queries(index, graph, {1, 2, 3, 4}, filename);
}

static void check_root(const std::string& prefix) {
    HashGraph graph;
    auto n1 = graph.create_handle("A", 1);
    auto n2 = graph.create_handle("G", 2);
    graph.create_edge(n1, n2);
    graph.create_edge(graph.flip(n1), n1);
    graph.create_edge(n2, graph.flip(n2));

    IntegratedSnarlFinder finder(graph);
    SnarlDistanceIndex index;
    fill_in_distance_index(&index, &graph, &finder);
    if (index.minimum_distance(1, false, 0, 2, true, 0) != 2
        || index.minimum_distance(1, true, 0, 2, true, 0) != 3) {
        throw std::runtime_error("root query mismatch");
    }
    std::string filename = prefix + ".root.dist";
    save(index, filename);
    dump_and_check_queries(index, graph, {1, 2}, filename);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: distance_fixture OUTPUT_PREFIX\n";
        return 2;
    }
    check_bubble(argv[1], 100, "ordinary");
    check_bubble(argv[1], 1, "oversized");
    check_root(argv[1]);
    std::cout << "PASS ordinary oversized root queries\n";
}
