#ifndef VG_SHARED_TRANSCRIPT_PATH_HPP_INCLUDED
#define VG_SHARED_TRANSCRIPT_PATH_HPP_INCLUDED

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace vg {

/**
 * An edited walk described by slices of shared, immutable source walks.
 * Mapping must provide handle, offset and length, as EditedMapping does.
 * Source mappings must cover whole nodes; their lengths remain valid even
 * after the graph is split. Only the first and last step of a slice are clipped.
 * This representation does not collapse transcript identities or equal walks.
 *
 * Transcriptome uses this on its GBWT reference/no-collapse route.
 */
template<class Mapping>
class SharedTranscriptPath {
public:
    class Source {
    public:
        explicit Source(std::vector<Mapping> mappings) : mappings_(std::move(mappings)) {
            // Validate once per source, not once per transcript occurrence.
            for (const Mapping& mapping : mappings_) {
                if (mapping.offset != 0 || mapping.length == 0) {
                    throw std::invalid_argument("Shared transcript source must contain whole nodes");
                }
            }
        }
        uint64_t size() const { return mappings_.size(); }
        const Mapping& operator[](uint64_t index) const { return mappings_[index]; }
        uint64_t capacity_bytes() const { return mappings_.capacity() * sizeof(Mapping); }
    private:
        const std::vector<Mapping> mappings_;
    };

    struct Slice {
        std::shared_ptr<const Source> source;
        uint64_t begin;
        uint64_t end;
        uint32_t first_offset;
        uint32_t last_end;
    };

    SharedTranscriptPath() = default;
    SharedTranscriptPath(const SharedTranscriptPath&) = default;
    SharedTranscriptPath& operator=(const SharedTranscriptPath&) = default;
    SharedTranscriptPath(SharedTranscriptPath&& other) noexcept :
        slices_(std::move(other.slices_)),
        step_count_(std::exchange(other.step_count_, 0)),
        reverse_(std::exchange(other.reverse_, false)) {}
    SharedTranscriptPath& operator=(SharedTranscriptPath&& other) noexcept {
        if (this != &other) {
            slices_ = std::move(other.slices_);
            step_count_ = std::exchange(other.step_count_, 0);
            reverse_ = std::exchange(other.reverse_, false);
        }
        return *this;
    }

    /** Append [begin, end), clipping the first offset and last exclusive end. */
    void append(const std::shared_ptr<const Source>& source, uint64_t begin, uint64_t end,
                uint32_t first_offset, uint32_t last_end) {
        if (!source || begin >= end || end > source->size()) {
            throw std::invalid_argument("Invalid shared transcript slice");
        }
        const Mapping& first = (*source)[begin];
        const Mapping& last = (*source)[end - 1];
        if (first_offset >= first.length || last_end == 0 || last_end > last.length ||
            (end == begin + 1 && first_offset >= last_end)) {
            throw std::invalid_argument("Invalid shared transcript slice boundaries");
        }
        if (end - begin > std::numeric_limits<uint64_t>::max() - step_count_) {
            throw std::overflow_error("Shared transcript step count overflow");
        }
        // Adjacent whole-node slices have the same expanded walk when joined.
        // A partial boundary or repeated source position must remain separate.
        if (!slices_.empty()) {
            Slice& previous = slices_.back();
            if (previous.source == source && previous.end == begin &&
                previous.last_end == (*source)[begin - 1].length && first_offset == 0) {
                previous.end = end;
                previous.last_end = last_end;
                step_count_ += end - begin;
                return;
            }
        }
        slices_.push_back({source, begin, end, first_offset, last_end});
        step_count_ += end - begin;
    }

    uint64_t size() const { return step_count_; }
    bool empty() const { return step_count_ == 0; }
    const std::vector<Slice>& slices() const { return slices_; }
    bool is_reverse() const { return reverse_; }
    void reverse_complement() { reverse_ = !reverse_; }

    /**
     * Callback receives a temporary mapping and its zero-based position in
     * this walk. The mapping reference, if the callback binds one, is valid
     * only for that callback invocation.
     */
    template<class Flip, class Iteratee>
    void for_each_mapping(const Flip& flip, const Iteratee& iteratee) const {
        uint64_t rank = 0;
        if (!reverse_) {
            for (const Slice& slice : slices_) {
                for (uint64_t i = slice.begin; i < slice.end; ++i) {
                    iteratee(mapping_at(slice, i, flip), rank++);
                }
            }
        } else {
            for (auto slice = slices_.rbegin(); slice != slices_.rend(); ++slice) {
                for (uint64_t i = slice->end; i > slice->begin;) {
                    iteratee(mapping_at(*slice, --i, flip), rank++);
                }
            }
        }
    }


private:
    template<class Flip>
    Mapping mapping_at(const Slice& slice, uint64_t index, const Flip& flip) const {
        Mapping mapping = (*slice.source)[index];
        const uint32_t original_length = mapping.length;
        mapping.offset = index == slice.begin ? slice.first_offset : 0;
        const uint32_t end = index + 1 == slice.end ? slice.last_end : original_length;
        mapping.length = end - mapping.offset;
        if (reverse_) {
            mapping.handle = flip(mapping.handle);
            mapping.offset = original_length - end;
        }
        return mapping;
    }

    std::vector<Slice> slices_;
    uint64_t step_count_ = 0;
    bool reverse_ = false;
};

}
#endif
