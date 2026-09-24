#ifndef VG_SHARED_TRANSCRIPT_PATH_HPP_INCLUDED
#define VG_SHARED_TRANSCRIPT_PATH_HPP_INCLUDED

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
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

    /**
     * Translate the shared sources used by a batch after graph nodes are split
     * or renumbered. Register every path in the batch before translating any
     * path. Sources are translated lazily, once, and retained only until their
     * final registered slice has been consumed.
     */
    class TranslationCache;

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

    /** Callback receives a mapping and its zero-based position in this walk. */
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

template<class Mapping>
class SharedTranscriptPath<Mapping>::TranslationCache {
    using Path = SharedTranscriptPath<Mapping>;
    using Source = typename Path::Source;
    using Slice = typename Path::Slice;

    struct Boundary {
        uint64_t mapping_index;
        uint32_t offset;

        bool operator==(const Boundary& other) const {
            return mapping_index == other.mapping_index && offset == other.offset;
        }
    };

    struct BoundaryHash {
        size_t operator()(const Boundary& boundary) const noexcept {
            const size_t first = std::hash<uint64_t>{}(boundary.mapping_index);
            const size_t second = std::hash<uint32_t>{}(boundary.offset);
            return first ^ (second + static_cast<size_t>(0x9e3779b9U) +
                            (first << 6) + (first >> 2));
        }
    };

    using BoundaryMap = std::unordered_map<Boundary, uint64_t, BoundaryHash>;

    struct Entry {
        // The raw pointer is the hash key; the weak owner distinguishes reuse
        // of the same address without retaining the old source.
        std::weak_ptr<const Source> original;
        uint64_t remaining_slices = 0;
        std::vector<std::pair<uint64_t, uint64_t>> covered_intervals;
        BoundaryMap translated_boundaries;
        std::shared_ptr<const Source> translated_source;
        std::vector<uint64_t> old_to_new;
    };

    struct PendingRegistration {
        std::weak_ptr<const Source> original;
        uint64_t slices = 0;
        std::vector<std::pair<uint64_t, uint64_t>> covered_intervals;
        std::vector<Boundary> internal_boundaries;
    };

public:
    TranslationCache() = default;
    TranslationCache(const TranslationCache&) = delete;
    TranslationCache& operator=(const TranslationCache&) = delete;
    TranslationCache(TranslationCache&&) = delete;
    TranslationCache& operator=(TranslationCache&&) = delete;

    /**
     * Register one path from the batch. Registration retains no additional
     * strong reference to an old Source and is closed by the first translate().
     */
    void register_path(const Path& path) {
        if (translation_started_) {
            throw std::logic_error("Cannot register shared paths after translation begins");
        }
        if (path.slices().size() >
            std::numeric_limits<uint64_t>::max() - remaining_slices_) {
            throw std::overflow_error("Shared translation slice count overflow");
        }

        std::unordered_map<const Source*, PendingRegistration> pending;
        pending.reserve(path.slices().size());
        for (const Slice& slice : path.slices()) {
            const Source* key = slice.source.get();
            auto inserted = pending.try_emplace(key);
            PendingRegistration& registration = inserted.first->second;
            if (inserted.second) {
                registration.original = slice.source;
            } else if (!same_owner(registration.original, slice.source)) {
                throw std::logic_error("Shared source address was reused during registration");
            }
            if (registration.slices == std::numeric_limits<uint64_t>::max()) {
                throw std::overflow_error("Shared translation source slice count overflow");
            }
            ++registration.slices;
            registration.covered_intervals.emplace_back(slice.begin, slice.end);
            add_internal_boundary(registration, slice.source, slice.begin,
                                  slice.first_offset);
            add_internal_boundary(registration, slice.source, slice.end - 1,
                                  slice.last_end);
        }

        // Reject all deterministic errors before changing cache counts.
        for (const auto& item : pending) {
            auto found = entries_.find(item.first);
            if (found != entries_.end()) {
                if (!same_owner(found->second.original, item.second.original)) {
                    throw std::logic_error("Shared source address was reused between registrations");
                }
                if (item.second.slices > std::numeric_limits<uint64_t>::max() -
                                             found->second.remaining_slices) {
                    throw std::overflow_error("Shared translation source slice count overflow");
                }
            }
        }

        for (auto& item : pending) {
            auto inserted = entries_.try_emplace(item.first);
            Entry& entry = inserted.first->second;
            if (inserted.second) {
                entry.original = item.second.original;
            }
            entry.covered_intervals.insert(entry.covered_intervals.end(),
                                           item.second.covered_intervals.begin(),
                                           item.second.covered_intervals.end());
            for (const Boundary& boundary : item.second.internal_boundaries) {
                entry.translated_boundaries.try_emplace(boundary, 0);
            }
            entry.remaining_slices += item.second.slices;
        }
        remaining_slices_ += static_cast<uint64_t>(path.slices().size());
    }

    /**
     * Translate a registered path without expanding it mapping-by-mapping.
     * mapper(original, emit) must emit positive-length, offset-zero mappings
     * in the original orientation whose lengths partition original.length.
     * Only source mappings covered by at least one registered slice are sent
     * to mapper.
     */
    template<class Mapper>
    Path translate(const Path& path, Mapper&& mapper) {
        translation_started_ = true;

        std::unordered_map<const Source*, uint64_t> uses;
        uses.reserve(path.slices().size());
        for (const Slice& slice : path.slices()) {
            const Source* key = slice.source.get();
            auto found = entries_.find(key);
            if (found == entries_.end() ||
                !same_owner(found->second.original, slice.source)) {
                throw std::logic_error("Attempted to translate an unregistered shared source");
            }
            uint64_t& count = uses[key];
            if (count == std::numeric_limits<uint64_t>::max()) {
                throw std::overflow_error("Shared translation path slice count overflow");
            }
            ++count;
        }
        for (const auto& use : uses) {
            if (use.second > entries_.at(use.first).remaining_slices) {
                throw std::logic_error("Shared source slices were translated too many times");
            }
        }

        // Preserve first-encounter mapper order while still translating each
        // unique source only once.
        for (const Slice& slice : path.slices()) {
            Entry& entry = entries_.at(slice.source.get());
            if (!entry.translated_source) {
                translate_source(*slice.source, entry, mapper);
            }
        }

        // Construct the entire result before consuming registrations. A bad
        // clipping boundary therefore never returns or commits a partial path.
        Path translated;
        for (const Slice& slice : path.slices()) {
            const Entry& entry = entries_.at(slice.source.get());
            const uint64_t begin = translated_boundary(entry, slice,
                                                       slice.begin,
                                                       slice.first_offset);
            const uint64_t end = translated_boundary(entry, slice,
                                                     slice.end - 1,
                                                     slice.last_end);
            if (begin >= end || end > entry.translated_source->size()) {
                throw std::logic_error("Translated shared slice is empty or out of range");
            }
            translated.append(entry.translated_source, begin, end, 0,
                              (*entry.translated_source)[end - 1].length);
        }
        if (path.is_reverse()) {
            translated.reverse_complement();
        }

        for (const auto& use : uses) {
            auto found = entries_.find(use.first);
            Entry& entry = found->second;
            if (use.second > entry.remaining_slices ||
                use.second > remaining_slices_) {
                throw std::logic_error("Shared translation slice counter underflow");
            }
            entry.remaining_slices -= use.second;
            remaining_slices_ -= use.second;
            if (entry.remaining_slices == 0) {
                entries_.erase(found);
            }
        }
        return translated;
    }

    uint64_t remaining() const noexcept { return remaining_slices_; }
    bool empty() const noexcept { return remaining_slices_ == 0; }

private:
    static bool same_owner(const std::weak_ptr<const Source>& left,
                           const std::shared_ptr<const Source>& right) {
        return !left.owner_before(right) && !right.owner_before(left);
    }

    static bool same_owner(const std::weak_ptr<const Source>& left,
                           const std::weak_ptr<const Source>& right) {
        return !left.owner_before(right) && !right.owner_before(left);
    }

    static void add_internal_boundary(PendingRegistration& registration,
                                      const std::shared_ptr<const Source>& source,
                                      uint64_t mapping_index, uint32_t offset) {
        const uint32_t length = (*source)[mapping_index].length;
        if (offset != 0 && offset != length) {
            registration.internal_boundaries.push_back({mapping_index, offset});
        }
    }

    template<class Mapper>
    static void translate_source(const Source& original, Entry& entry,
                                 Mapper& mapper) {
        if (original.size() == std::numeric_limits<uint64_t>::max()) {
            throw std::overflow_error("Shared source is too large to translate");
        }
        const size_t source_size = static_cast<size_t>(original.size());
        std::vector<int64_t> coverage_events(source_size + 1, 0);
        for (const auto& interval : entry.covered_intervals) {
            if (interval.first >= interval.second || interval.second > original.size()) {
                throw std::logic_error("Registered shared source interval is invalid");
            }
            ++coverage_events[static_cast<size_t>(interval.first)];
            --coverage_events[static_cast<size_t>(interval.second)];
        }

        std::vector<Mapping> mappings;
        mappings.reserve(source_size);
        std::vector<uint64_t> old_to_new(source_size + 1, 0);
        BoundaryMap resolved;
        resolved.reserve(entry.translated_boundaries.size());

        int64_t coverage = 0;
        for (uint64_t i = 0; i < original.size(); ++i) {
            coverage += coverage_events[static_cast<size_t>(i)];
            if (coverage < 0) {
                throw std::logic_error("Shared source coverage counter underflow");
            }
            old_to_new[static_cast<size_t>(i)] = mappings.size();
            if (coverage == 0) {
                continue;
            }

            const Mapping& old_mapping = original[i];
            uint64_t emitted_length = 0;
            auto emit = [&](const Mapping& new_mapping) {
                if (new_mapping.offset != 0 || new_mapping.length == 0) {
                    throw std::invalid_argument(
                        "Translated shared source must contain whole nodes");
                }
                if (emitted_length > old_mapping.length ||
                    new_mapping.length > old_mapping.length - emitted_length) {
                    throw std::invalid_argument(
                        "Translated shared mapping exceeds original length");
                }
                mappings.push_back(new_mapping);
                emitted_length += new_mapping.length;
                const Boundary boundary{i, static_cast<uint32_t>(emitted_length)};
                if (entry.translated_boundaries.find(boundary) !=
                    entry.translated_boundaries.end()) {
                    resolved.emplace(boundary, mappings.size());
                }
            };
            mapper(old_mapping, emit);
            if (emitted_length != old_mapping.length) {
                throw std::invalid_argument(
                    "Translated shared mapping does not preserve original length");
            }
        }
        coverage += coverage_events[source_size];
        if (coverage != 0) {
            throw std::logic_error("Shared source coverage counter did not close");
        }
        old_to_new[source_size] = mappings.size();
        if (resolved.size() != entry.translated_boundaries.size()) {
            throw std::invalid_argument(
                "Shared slice boundary does not align to a translated node boundary");
        }

        auto translated_source = std::make_shared<const Source>(std::move(mappings));
        entry.translated_source = std::move(translated_source);
        entry.old_to_new = std::move(old_to_new);
        entry.translated_boundaries = std::move(resolved);
        std::vector<std::pair<uint64_t, uint64_t>>().swap(entry.covered_intervals);
    }

    static uint64_t translated_boundary(const Entry& entry, const Slice& slice,
                                        uint64_t mapping_index, uint32_t offset) {
        const uint32_t old_length = (*slice.source)[mapping_index].length;
        if (offset == 0) {
            return entry.old_to_new.at(static_cast<size_t>(mapping_index));
        }
        if (offset == old_length) {
            return entry.old_to_new.at(static_cast<size_t>(mapping_index + 1));
        }
        auto found = entry.translated_boundaries.find({mapping_index, offset});
        if (found == entry.translated_boundaries.end()) {
            throw std::logic_error("Translated shared slice boundary is missing");
        }
        return found->second;
    }

    std::unordered_map<const Source*, Entry> entries_;
    uint64_t remaining_slices_ = 0;
    bool translation_started_ = false;
};

} // namespace vg

#endif
