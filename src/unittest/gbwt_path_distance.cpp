/** \file
 *
 * Unit tests for gbwt_path_distance(), which measures intron lengths along unspliced GBWT threads.
 */

#include "../algorithms/ref_path_distance.hpp"
#include "../gbwt_helper.hpp"

#include <bdsg/hash_graph.hpp>

#include "catch.hpp"

namespace vg {
namespace unittest {

static gbwt::vector_type forward_thread(const vector<nid_t>& ids) {
    gbwt::vector_type thread;
    for (nid_t id : ids) {
        thread.push_back(gbwt::Node::encode(id, false));
    }
    return thread;
}

TEST_CASE("gbwt_path_distance measures along unspliced threads", "[gbwt_path_distance]") {

    // exon A (1), intron 1 (2), exon B (3), intron 2 (4), exon C (5), with the genomic edges
    // through the introns and the splice edges A->B and B->C
    bdsg::HashGraph graph;
    handle_t a = graph.create_handle(string(10, 'A'), 1);
    handle_t i1 = graph.create_handle(string(100, 'C'), 2);
    handle_t b = graph.create_handle(string(20, 'G'), 3);
    handle_t i2 = graph.create_handle(string(200, 'T'), 4);
    handle_t c = graph.create_handle(string(10, 'A'), 5);
    graph.create_edge(a, i1);
    graph.create_edge(i1, b);
    graph.create_edge(b, i2);
    graph.create_edge(i2, c);
    graph.create_edge(a, b);
    graph.create_edge(b, c);

    gbwt::vector_type body = forward_thread({1, 2, 3, 4, 5});
    gbwt::vector_type transcript = forward_thread({1, 3, 5});

    // the last base of A is at coordinate 9 and the first base of C at 10 + 100 + 20 + 200 = 330;
    // the spliced route A->B->C would put C at 30 instead
    pos_t end_of_a = make_pos_t(1, false, 9);
    pos_t start_of_c = make_pos_t(5, false, 0);
    const int64_t unmeasured = numeric_limits<int64_t>::max();

    SECTION("A gene-body thread gives the genomic distance") {
        gbwt::GBWT index = get_gbwt({body});
        REQUIRE(algorithms::gbwt_path_distance(index, graph, end_of_a, start_of_c, 1000, 32) == 321);
    }

    SECTION("With a spliced transcript thread as well, the longer body thread is used") {
        gbwt::GBWT index = get_gbwt({transcript, body});
        REQUIRE(algorithms::gbwt_path_distance(index, graph, end_of_a, start_of_c, 1000, 32) == 321);
    }

    SECTION("The reverse strand gives the same distance") {
        gbwt::GBWT index = get_gbwt({body});
        pos_t rev_end_of_c = make_pos_t(5, true, 9);
        pos_t rev_start_of_a = make_pos_t(1, true, 0);
        REQUIRE(algorithms::gbwt_path_distance(index, graph, rev_end_of_c, rev_start_of_a, 1000, 32) == 321);
    }

    SECTION("Positions farther apart than the limit are unmeasured") {
        gbwt::GBWT index = get_gbwt({body});
        REQUIRE(algorithms::gbwt_path_distance(index, graph, end_of_a, start_of_c, 300, 32) == unmeasured);
    }

    SECTION("Positions that no thread connects are unmeasured") {
        gbwt::GBWT index = get_gbwt({forward_thread({1, 2, 3}), forward_thread({4, 5})});
        REQUIRE(algorithms::gbwt_path_distance(index, graph, end_of_a, start_of_c, 1000, 32) == unmeasured);
    }
}

}
}
