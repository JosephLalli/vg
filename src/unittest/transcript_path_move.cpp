/// \file transcript_path_move.cpp
///
/// Regression tests for transcript-path move operations.

#include "../transcriptome.hpp"
#include <handlegraph/util.hpp>

#include "catch.hpp"

namespace vg {
namespace unittest {

TEST_CASE("Transcript paths move their path state", "[transcript_path]") {
    SECTION("base transcript paths move metadata buffers") {
        TranscriptPath source("tx", "embedded", true, false);
        source.transcript_names.emplace_back("alias");
        source.embedded_path_names.emplace_back("alt", true);
        auto* construction_buffer = source.transcript_names.data();
        auto* construction_embedded_buffer = source.embedded_path_names.data();
        TranscriptPath constructed(std::move(source));
        REQUIRE(constructed.transcript_names.data() == construction_buffer);
        REQUIRE(constructed.embedded_path_names.data() == construction_embedded_buffer);
        REQUIRE(constructed.transcript_names == vector<string>{"tx", "alias"});
        REQUIRE(constructed.embedded_path_names == vector<pair<string, bool>>{{"embedded", true}, {"alt", true}});

        auto* assignment_buffer = constructed.transcript_names.data();
        auto* assignment_embedded_buffer = constructed.embedded_path_names.data();
        TranscriptPath assigned("other", "path", true, false);
        assigned = std::move(constructed);
        REQUIRE(assigned.transcript_names.data() == assignment_buffer);
        REQUIRE(assigned.embedded_path_names.data() == assignment_embedded_buffer);
        REQUIRE(assigned.transcript_names == vector<string>{"tx", "alias"});

        TranscriptPath copied = assigned;
        copied = assigned;
        assigned.transcript_names.front() = "changed";
        REQUIRE(copied.transcript_names.front() == "tx");
    }

    SECTION("edited transcript paths move metadata and protobuf path data") {
        EditedTranscriptPath source("tx", "embedded", true, false);
        auto* mapping = source.path.add_mapping();
        mapping->mutable_position()->set_node_id(42);
        mapping->mutable_position()->set_is_reverse(true);
        auto* construction_buffer = source.transcript_names.data();
        EditedTranscriptPath constructed(std::move(source));
        REQUIRE(constructed.transcript_names.data() == construction_buffer);
        REQUIRE(constructed.path.mapping_size() == 1);
        REQUIRE(&constructed.path.mapping(0) == mapping);
        REQUIRE(constructed.path.mapping(0).position().node_id() == 42);
        REQUIRE(constructed.path.mapping(0).position().is_reverse());

        auto* assignment_buffer = constructed.transcript_names.data();
        auto* assignment_mapping = &constructed.path.mapping(0);
        EditedTranscriptPath assigned("other", "path", true, false);
        assigned = std::move(constructed);
        REQUIRE(assigned.transcript_names.data() == assignment_buffer);
        REQUIRE(&assigned.path.mapping(0) == assignment_mapping);
        REQUIRE(assigned.path.mapping(0).position().node_id() == 42);
        REQUIRE(assigned.path.mapping(0).position().is_reverse());
    }

    SECTION("completed transcript paths move completed handles") {
        EditedTranscriptPath edited("tx", "embedded", true, false);
        CompletedTranscriptPath source(edited);
        const vector<handle_t> expected{handlegraph::number_bool_packing::pack(3, false),
                                       handlegraph::number_bool_packing::pack(7, true)};
        source.path = expected;
        auto* construction_buffer = source.path.data();
        CompletedTranscriptPath constructed(std::move(source));
        REQUIRE(constructed.path.data() == construction_buffer);
        REQUIRE(constructed.path == expected);

        auto* assignment_buffer = constructed.path.data();
        CompletedTranscriptPath assigned(edited);
        assigned = std::move(constructed);
        REQUIRE(assigned.path.data() == assignment_buffer);
        REQUIRE(assigned.path == expected);
    }
}

}
}
