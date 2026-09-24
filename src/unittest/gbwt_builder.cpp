#include "catch.hpp"

#include <gbwt/dynamic_gbwt.h>

#include <limits>
#include <sstream>

namespace unittest {
namespace {

std::vector<gbwt::vector_type> insertion_paths() {
    std::vector<gbwt::vector_type> paths;
    // Both orientations exceed the worker threshold initially, then empty and
    // short paths end mid-batch. Shared targets, cycles and duplicate paths
    // exercise incoming counts and stable sequence ordering.
    for (size_t i = 0; i < 10000; ++i) {
        gbwt::vector_type path;
        size_t length = (i % 11 == 0 ? 0 : 1 + i % 13);
        for (size_t j = 0; j < length; ++j) {
            path.push_back(gbwt::Node::encode(1 + (i + j * j) % 97, j % 3 == 0));
        }
        paths.push_back(std::move(path));
    }
    return paths;
}

gbwt::DynamicGBWT insert_paths(const std::vector<gbwt::vector_type>& paths,
                             size_t batch_nodes, size_t sampling, size_t threads) {
    gbwt::GBWTBuilder builder(32, batch_nodes, sampling, threads);
    for (const auto& path : paths) { builder.insert(path, true); }
    builder.finish();
    builder.index.addMetadata();
    builder.index.metadata.setSamples({"sample"});
    builder.index.metadata.setContigs({"contig"});
    builder.index.metadata.setHaplotypes(1);
    for (size_t i = 0; i < paths.size(); ++i) { builder.index.metadata.addPath(0, 0, 0, i); }
    return std::move(builder.index);
}

template<class Index>
std::string serialized_index(const Index& index) {
    std::ostringstream out(std::ios::binary);
    index.serialize(out);
    return out.str();
}

} // namespace

TEST_CASE("GBWT insertion workers preserve sequences, metadata and serialization", "[gbwt-builder]") {
    const auto paths = insertion_paths();
    for (size_t batch_nodes : {size_t(1000000), size_t(90000)}) {
        for (size_t sampling : {size_t(0), size_t(3)}) {
            const auto serial = insert_paths(paths, batch_nodes, sampling, 1);
            const auto serial_bytes = serialized_index(serial);
            const auto compressed_bytes = serialized_index(gbwt::GBWT(serial));
            for (size_t threads : {size_t(2), size_t(4), size_t(24)}) {
                INFO("batch " << batch_nodes << ", sampling " << sampling << ", threads " << threads);
                const auto parallel = insert_paths(paths, batch_nodes, sampling, threads);
                REQUIRE(serialized_index(parallel) == serial_bytes);
                REQUIRE(serialized_index(gbwt::GBWT(parallel)) == compressed_bytes);
                std::istringstream in(compressed_bytes, std::ios::binary);
                gbwt::GBWT reloaded;
                reloaded.load(in);
                REQUIRE(reloaded.metadata == parallel.metadata);
                REQUIRE(reloaded.sequences() == 2 * paths.size());
                for (size_t i = 0; i < paths.size(); ++i) {
                    REQUIRE(reloaded.extract(2 * i) == paths[i]);
                    auto reverse = paths[i];
                    gbwt::reversePath(reverse);
                    REQUIRE(reloaded.extract(2 * i + 1) == reverse);
                }
            }
        }
    }
}

TEST_CASE("Failed GBWT insertion remains poisoned and propagates to the caller", "[gbwt-builder]") {
    for (size_t threads : {size_t(2), size_t(4), size_t(24)}) {
        gbwt::GBWTBuilder builder(32, 200000, 1, threads);
        gbwt::vector_type first{gbwt::Node::encode(1, false), gbwt::Node::encode(3, false)};
        gbwt::vector_type second{gbwt::Node::encode(2, false), gbwt::Node::encode(3, false)};
        builder.insert(first, true);
        builder.insert(second, true);
        builder.finish();
        // Deliberately saturate an incoming count. The next batch must report
        // its overflow from inside an offset worker, not terminate or recode.
        auto& record = builder.index.record(gbwt::Node::encode(3, false));
        REQUIRE(record.indegree() == 2);
        record.count(0) = std::numeric_limits<gbwt::edge_type::second_type>::max();
        for (size_t i = 0; i < 8192; ++i) { builder.insert(i % 2 ? first : second, true); }
        REQUIRE_THROWS_AS(builder.finish(), std::overflow_error);
        const auto failed_bytes = serialized_index(builder.index);
        auto input_tail = builder.input_tail;
        auto inserted = builder.inserted_sequences;
        REQUIRE_THROWS_AS(builder.insert(first, true), std::overflow_error);
        REQUIRE_THROWS_AS(builder.finish(), std::overflow_error);
        gbwt::DynamicGBWT other;
        REQUIRE_THROWS_AS(builder.swapIndex(other), std::overflow_error);
        REQUIRE(other.empty());
        REQUIRE(builder.input_tail == input_tail);
        REQUIRE(builder.inserted_sequences == inserted);
        REQUIRE(serialized_index(builder.index) == failed_bytes);
    }
}

TEST_CASE("Zero GBWT insertion workers normalizes to one", "[gbwt-builder]") {
    std::vector<gbwt::vector_type> paths{{}, {2}, {2, 4, 2}, {5, 4, 2}};
    REQUIRE(serialized_index(insert_paths(paths, 13, 1, 0)) ==
            serialized_index(insert_paths(paths, 13, 1, 1)));
}

} // namespace unittest
