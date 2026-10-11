#include <cstdint>
#include <limits>
#include <type_traits>

#include "../transcriptome.hpp"
#include "catch.hpp"

namespace vg {
namespace unittest {

TEST_CASE("Edited mappings preserve the protobuf numeric domain",
          "[transcriptome][edited_mapping]") {

    STATIC_REQUIRE((std::is_same<decltype(EditedMapping::offset),
                                 decltype(Position().offset())>::value));
    STATIC_REQUIRE((std::is_same<decltype(EditedMapping::length),
                                 decltype(Edit().from_length())>::value));

    const int64_t offset = static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) + 1;
    const int32_t length = std::numeric_limits<int32_t>::max();
    const EditedMapping edited{handlegraph::as_handle(1), offset, length};

    Mapping protobuf_mapping;
    protobuf_mapping.mutable_position()->set_offset(edited.offset);
    protobuf_mapping.add_edit()->set_from_length(edited.length);

    const EditedMapping round_trip{
        edited.handle,
        protobuf_mapping.position().offset(),
        protobuf_mapping.edit(0).from_length()
    };

    REQUIRE(round_trip == edited);
    REQUIRE(round_trip.offset > std::numeric_limits<uint32_t>::max());
    REQUIRE(round_trip.length == std::numeric_limits<int32_t>::max());
}

}
}
