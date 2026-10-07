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
 * Source steps cover whole nodes with 64-bit spans that remain valid even
 * after the graph is split. Every emitted Mapping length must fit its own type.
 * Only the first and last step of a slice are clipped.
 * This representation does not collapse transcript identities or equal walks.
 *
 * Transcriptome uses this on its GBWT reference/no-collapse route.
 */
template<class Mapping>
class SharedTranscriptPath {
public:
    using handle_type = decltype(Mapping::handle);

    struct SourceStep {
        handle_type handle;
        uint64_t length;
    };

    class Source {
    public:
        explicit Source(std::vector<Mapping> mappings) {
            mappings_.reserve(mappings.size());
            // Validate once per source, not once per transcript occurrence.
            for (const Mapping& mapping : mappings) {
                if (mapping.offset != 0 || mapping.length <= 0) {
                    throw std::invalid_argument("Shared transcript source must contain whole nodes");
                }
                mappings_.push_back({mapping.handle, static_cast<uint64_t>(mapping.length)});
            }
            validate();
        }
        explicit Source(std::vector<SourceStep> mappings) : mappings_(std::move(mappings)) {
            validate();
        }
        uint64_t size() const { return mappings_.size(); }
        const SourceStep& operator[](uint64_t index) const { return mappings_[index]; }
        uint64_t capacity_bytes() const { return mappings_.capacity() * sizeof(SourceStep); }
    private:
        void validate() {
            for (const SourceStep& mapping : mappings_) {
                if (mapping.length == 0 ||
                    mapping.length > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                    throw std::invalid_argument("Shared transcript node length exceeds Position range");
                }
                all_lengths_fit_mapping_ = all_lengths_fit_mapping_ &&
                    mapping.length <= static_cast<uint64_t>(std::numeric_limits<int32_t>::max());
            }
        }

        std::vector<SourceStep> mappings_;
        bool all_lengths_fit_mapping_ = true;

        friend class SharedTranscriptPath<Mapping>;
    };

    struct Slice {
        std::shared_ptr<const Source> source;
        uint64_t begin;
        uint64_t end;
        int64_t first_offset;
        uint64_t last_end;
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
                int64_t first_offset, uint64_t last_end) {
        if (!source || begin >= end || end > source->size()) {
            throw std::invalid_argument("Invalid shared transcript slice");
        }
        const SourceStep& first = (*source)[begin];
        const SourceStep& last = (*source)[end - 1];
        if (first_offset < 0 || static_cast<uint64_t>(first_offset) >= first.length ||
            last_end == 0 || last_end > last.length ||
            (end == begin + 1 && static_cast<uint64_t>(first_offset) >= last_end)) {
            throw std::invalid_argument("Invalid shared transcript slice boundaries");
        }
        if (!source->all_lengths_fit_mapping_) {
            for (uint64_t i = begin; i < end; ++i) {
                const uint64_t offset = i == begin ? static_cast<uint64_t>(first_offset) : 0;
                const uint64_t mapping_end = i + 1 == end ? last_end : (*source)[i].length;
                if (mapping_end <= offset ||
                    mapping_end - offset > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
                    throw std::invalid_argument("Shared transcript mapping length exceeds Edit range");
                }
            }
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

    /**
     * Visit only slice endpoints, in walk order, with their original ranks.
     * All potentially partial mappings occur here. Whole internal mappings
     * need not be expanded merely to discover graph breakpoints.
     */
    template<class Flip, class Iteratee>
    void for_each_boundary(const Flip& flip, const Iteratee& iteratee) const {
        uint64_t rank = 0;
        auto visit = [&](const Slice& slice) {
            const uint64_t count = slice.end - slice.begin;
            const uint64_t first = reverse_ ? slice.end - 1 : slice.begin;
            const uint64_t last = reverse_ ? slice.begin : slice.end - 1;
            iteratee(mapping_at(slice, first, flip), rank);
            if (count > 1) {
                iteratee(mapping_at(slice, last, flip), rank + count - 1);
            }
            rank += count;
        };
        if (!reverse_) {
            for (const Slice& slice : slices_) { visit(slice); }
        } else {
            for (auto slice = slices_.rbegin(); slice != slices_.rend(); ++slice) {
                visit(*slice);
            }
        }
    }

private:
    template<class Flip>
    Mapping mapping_at(const Slice& slice, uint64_t index, const Flip& flip) const {
        const SourceStep& source_mapping = (*slice.source)[index];
        const uint64_t original_length = source_mapping.length;
        const uint64_t offset = index == slice.begin
            ? static_cast<uint64_t>(slice.first_offset) : 0;
        const uint64_t end = index + 1 == slice.end ? slice.last_end : original_length;
        const uint64_t length = end - offset;
        Mapping mapping{source_mapping.handle, static_cast<int64_t>(offset),
                        static_cast<int32_t>(length)};
        if (reverse_) {
            mapping.handle = flip(mapping.handle);
            mapping.offset = static_cast<int64_t>(original_length - end);
        }
        return mapping;
    }

    std::vector<Slice> slices_;
    uint64_t step_count_ = 0;
    bool reverse_ = false;
};

}
#endif
