#include "catch.hpp"
#include "transcript_path_spool.hpp"
#include "utility.hpp"

#include <array>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace vg {
namespace unittest {

using namespace std;

static string new_spool_path() {
    string path = temp_file::create("transcript-path-spool");
    temp_file::remove(path);
    return path;
}

TEST_CASE("Transcript path spool preserves typed disk-backed spans", "[transcript_path_spool]") {
    const string path = new_spool_path();
    using Spool = TranscriptPathSpool<uint64_t>;
    {
        Spool spool(path);
        const array<uint64_t, 3> first = {{11, 12, 13}};
        const array<uint64_t, 4> second = {{21, 22, 23, 24}};
        auto first_span = spool.append(first.data(), first.size());
        auto empty_span = spool.append(nullptr, 0);
        auto second_span = spool.append(second.data(), second.size());
        REQUIRE(first_span.begin == 0);
        REQUIRE(first_span.count == 3);
        REQUIRE(empty_span.begin == 3);
        REQUIRE(empty_span.count == 0);
        REQUIRE(second_span.begin == 3);
        REQUIRE(second_span.count == 4);
        REQUIRE(spool.record_count() == 7);

        array<uint64_t, 2> subrange = {{0, 0}};
        spool.read(second_span, 1, subrange.size(), subrange.data());
        const array<uint64_t, 2> expected_subrange = {{22, 23}};
        REQUIRE(subrange == expected_subrange);

        vector<uint64_t> chunked;
        vector<uint64_t> chunk_sizes;
        spool.for_each_chunk(second_span, 2, [&](const uint64_t* records, uint64_t count) {
            chunk_sizes.push_back(count);
            chunked.insert(chunked.end(), records, records + count);
        });
        const vector<uint64_t> expected_chunk_sizes = {2, 2};
        const vector<uint64_t> expected_chunked = {21, 22, 23, 24};
        REQUIRE(chunk_sizes == expected_chunk_sizes);
        REQUIRE(chunked == expected_chunked);
        spool.for_each_chunk(empty_span, 2, [&](const uint64_t*, uint64_t) {
            FAIL("zero-length span must not invoke the callback");
        });
        spool.sync();

        REQUIRE_THROWS(spool.read(second_span, 4, 1, subrange.data()));
        REQUIRE_THROWS(spool.read(Spool::Span{7, 1}, 0, 1, subrange.data()));
        REQUIRE_THROWS(spool.for_each_chunk(first_span, 0, [](const uint64_t*, uint64_t) {}));
    }
    REQUIRE(access(path.c_str(), F_OK) == 0);
    temp_file::remove(path);
}

TEST_CASE("Transcript path spool supports 16-byte records and detects file errors", "[transcript_path_spool]") {
    struct EditedMapping {
        uint64_t handle;
        uint32_t offset;
        uint32_t length;
    };
    static_assert(sizeof(EditedMapping) == 16, "fixture must exercise 16-byte records");
    const string path = new_spool_path();
    using Spool = TranscriptPathSpool<EditedMapping>;
    {
        Spool spool(path);
        const array<EditedMapping, 3> records = {{{7, 1, 2}, {8, 3, 5}, {9, 8, 13}}};
        auto span = spool.append(records.data(), records.size());
        array<EditedMapping, 1> output = {{{0, 0, 0}}};
        spool.read(span, 2, 1, output.data());
        REQUIRE(output[0].handle == 9);
        REQUIRE(output[0].offset == 8);
        REQUIRE(output[0].length == 13);

        struct stat status;
        REQUIRE(::stat(path.c_str(), &status) == 0);
        REQUIRE(::truncate(path.c_str(), status.st_size - 1) == 0);
        REQUIRE_THROWS(spool.read(span, 2, 1, output.data()));
    }
    temp_file::remove(path);
}

TEST_CASE("Transcript path spool does not replace an existing file", "[transcript_path_spool]") {
    const string path = temp_file::create("transcript-path-spool-existing");
    {
        ofstream out(path, ios::binary | ios::trunc);
        out << "existing-data";
    }
    REQUIRE_THROWS(TranscriptPathSpool<uint64_t>{path});
    ifstream in(path, ios::binary);
    string contents((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    REQUIRE(contents == "existing-data");
    temp_file::remove(path);
}

}
}
