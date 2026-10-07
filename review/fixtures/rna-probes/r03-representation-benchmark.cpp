#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include "handlegraph/types.hpp"
#include "handlegraph/util.hpp"
#include "transcriptome.hpp"

using EditedMappingLayout = vg::EditedMapping;

static_assert(std::is_same_v<decltype(EditedMappingLayout::offset),
                             decltype(vg::Position().offset())>);
static_assert(std::is_same_v<decltype(EditedMappingLayout::length),
                             decltype(vg::Edit().from_length())>);

int main(int argc, char** argv) {
    const bool edited = argc == 2 && std::string(argv[1]) == "edited";
    constexpr size_t count = 500000;
    const int64_t offset_base =
        static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) + 1;
    uint64_t digest = 0;
    const auto start = std::chrono::steady_clock::now();

    std::vector<vg::Mapping> protobuf_mappings;
    std::vector<EditedMappingLayout> edited_mappings;
    if (edited) {
        edited_mappings.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            edited_mappings.push_back({
                handlegraph::number_bool_packing::pack(i + 1, i % 2),
                offset_base + static_cast<int64_t>(i % 1024),
                static_cast<int32_t>(17 + i % 31)
            });
        }
    } else {
        protobuf_mappings.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            auto& mapping = protobuf_mappings.emplace_back();
            auto* position = mapping.mutable_position();
            position->set_node_id(static_cast<int64_t>(i + 1));
            position->set_is_reverse(i % 2);
            position->set_offset(offset_base + static_cast<int64_t>(i % 1024));
            auto* edit = mapping.add_edit();
            edit->set_from_length(static_cast<int32_t>(17 + i % 31));
            edit->set_to_length(edit->from_length());
        }
    }
    const auto built = std::chrono::steady_clock::now();

    if (edited) {
        for (const auto& mapping : edited_mappings) {
            digest += handlegraph::number_bool_packing::unpack_number(mapping.handle);
            digest += static_cast<uint64_t>(
                handlegraph::number_bool_packing::unpack_bit(mapping.handle));
            digest += static_cast<uint64_t>(mapping.offset);
            digest += static_cast<uint64_t>(mapping.length);
        }
    } else {
        for (const auto& mapping : protobuf_mappings) {
            digest += static_cast<uint64_t>(mapping.position().node_id());
            digest += static_cast<uint64_t>(mapping.position().is_reverse());
            digest += static_cast<uint64_t>(mapping.position().offset());
            digest += static_cast<uint64_t>(mapping.edit(0).from_length());
        }
    }
    const auto end = std::chrono::steady_clock::now();
    std::cout << "arm=" << (edited ? "edited" : "protobuf")
              << " object_bytes=" << (edited ? sizeof(EditedMappingLayout) : sizeof(vg::Mapping))
              << " build_seconds=" << std::chrono::duration<double>(built - start).count()
              << " total_seconds=" << std::chrono::duration<double>(end - start).count()
              << " digest=" << digest << "\n";
}
