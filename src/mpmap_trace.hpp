#ifndef VG_MPMAP_TRACE_HPP_INCLUDED
#define VG_MPMAP_TRACE_HPP_INCLUDED

/**
 * \file mpmap_trace.hpp
 *
 * Instrumentation ("PR1-style trace mode") for vg mpmap.
 *
 * PURPOSE
 * -------
 * This layer measures, per read (or per mate of a pair), the search space that
 * mpmap's current MEM-based seeding / clustering / soft-clip-gated splice rescue
 * actually creates. It exists to answer -- empirically, not by assumption -- the
 * question of whether keeping graph MEMs is sufficient for STAR-like novel
 * splice-junction discovery, or whether a STAR-like sequential seed / MMP front
 * end is warranted. It does NOT change mapping behavior and does NOT implement a
 * new aligner. See the field-level comments below for what each metric feeds into
 * the MEM-vs-seed decision.
 *
 * DESIGN
 * ------
 * The trace is completely disabled unless a runtime output file is set via
 * mpmap_trace::open(). When disabled, the only cost is a single boolean branch at
 * each collection site (see enabled()). To avoid touching multipath_mapper.hpp
 * (which would recompile every includer), the sink is a set of free functions and
 * a thread_local "current record" pointer: MultipathMapper::multipath_map points
 * the thread_local at a stack-local record, and the shared splice helpers read it
 * back with `if (auto* tr = mpmap_trace::current())`. Each OpenMP worker maps whole
 * reads sequentially, so thread_local is the correct isolation primitive.
 */

#include <cstdint>
#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <limits>

namespace vg {

// Forward declaration; the proxy is defined in terms of raw MEMs without pulling
// in the (heavy) mem.hpp here.
class MaximalExactMatch;

namespace mpmap_trace {

/// Sentinel for "no second-best alignment" / "no splice score".
constexpr int64_t NO_SCORE = std::numeric_limits<int64_t>::min();

/// One graph placement (a mapping of an alignment), for external truth join.
struct TracePosition {
    int64_t node_id = 0;
    int64_t offset = 0;
    bool is_reverse = false;
};

/// Per-local-alignment record used as a candidate splice anchor. Captures the
/// soft-clip / rescue gate state and the candidate pool the current rescue saw.
/// One record is emitted per evaluation of the soft-clip gate in
/// find_spliced_alignments (an anchor re-visited after a successful splice may
/// appear more than once; group by anchor_index externally if needed).
struct SpliceAnchorTrace {
    int64_t anchor_index = 0;
    int64_t aligned_interval_begin = 0;
    int64_t aligned_interval_end = 0;
    int64_t left_tail_len = 0;
    int64_t right_tail_len = 0;
    int64_t left_tail_max_score = 0;
    int64_t right_tail_max_score = 0;
    bool search_left = false;   // soft-clip gate opened on the left
    bool search_right = false;  // soft-clip gate opened on the right
    bool adapter_rejected = false;

    // candidate pool for this anchor (summed over the sides that were searched)
    int64_t n_aligned_splice_candidates = 0;          // other local mp alns
    int64_t n_unaligned_cluster_candidates = 0;        // whole clusters not yet aligned
    int64_t n_unclustered_raw_mem_hit_candidates = 0;  // raw MEM hits
    int64_t n_total_splice_candidates = 0;
    int64_t n_candidates_aligned = 0;                  // had an alignment for the splice test
    int64_t n_candidates_rejected_before_alignment = 0; // unaligned candidates that failed to align
    int64_t n_candidates_rejected_after_alignment = 0;  // aligned but not chosen as the splice partner

    // splice motif / join enumeration for this anchor
    int64_t n_motif_pairs_examined = 0;
    bool motif_pairs_capped = false;      // hit max_motif_pairs budget
    int64_t n_splice_edges_considered = 0; // putative joins actually realigned
    int64_t n_splice_alignments_produced = 0; // joins crossing the significance gate
    int64_t best_splice_score = NO_SCORE;  // best net splice score for this anchor
    bool did_splice = false;               // a splice was accepted for this anchor
};

/// One conservatively-defined "unexplained" read interval that a proactive bounded
/// MEM-window stage would consider, plus how many raw MEM candidates fall in it.
/// This is a candidate-generation-only proxy: it never alters mapping output.
struct ProactiveIntervalTrace {
    int64_t interval_begin = 0;
    int64_t interval_end = 0;
    int side = 0;                 // -1 = left soft clip, +1 = right soft clip
    int64_t anchor_id = 0;        // index of the local alignment this tail came from
    int64_t tail_max_score = 0;   // best possible score of the soft-clipped tail
    int64_t candidate_mem_count = 0;
    int64_t candidate_hit_count = 0;
    int64_t best_candidate_mem_length = 0;
    int64_t max_hit_count = 0;
    bool current_gate_would_skip = false; // tail_max_score < min_softclipped_score_for_splice
};

/// Optional per-MEM detail, only emitted when a truth-junction file is supplied,
/// so external tooling can join MEM read-intervals / hit positions to truth.
struct MemDetail {
    int64_t read_begin = 0;   // offset from read start
    int64_t read_end = 0;
    int64_t length = 0;
    int64_t match_count = 0;   // true occurrence count (pre hit-cap)
    int64_t reported_hits = 0; // nodes.size() (post hit-cap)
    bool primary = false;      // SMEM (not a sub-MEM)
    std::vector<TracePosition> hit_positions; // capped
};

/// The full per-read (per-mate) trace record.
struct SpliceSearchTrace {
    // ---- read metadata ----
    std::string read_name;
    int64_t read_length = 0;
    bool is_paired = false;
    int mate = 0;                    // 0 = single/unpaired, 1 or 2 for a pair
    bool is_rna_mode = false;        // mapper configured for spliced alignment
    bool do_spliced_alignment = false; // splice search actually attempted this read
    bool mapping_succeeded = false;
    int64_t best_score = 0;
    int64_t second_best_score = NO_SCORE;
    int32_t mapq = 0;

    // ---- seed / MEM visibility (Q1, Q2) ----
    int64_t n_mems_total = 0;
    int64_t n_mem_hits_total = 0;          // sum of reported hits (nodes.size())
    int64_t n_mems_after_filtering = 0;    // MEMs long enough to seed clustering
    int64_t n_mem_hits_after_hit_max = 0;  // same as reported hits (post hit-cap)
    int64_t n_mem_true_occurrences = 0;    // sum of match_count (pre hit-cap)
    int64_t max_hits_per_mem = 0;
    int64_t sum_hits_for_short_mems = 0;   // hits on MEMs shorter than the splice min
    int64_t n_mems_hit_capped = 0;         // MEMs whose hits were truncated by hit_max
    int64_t n_high_fanout_mems = 0;
    // length bins: <8, 8-11, 12-15, 16-20, 21-30, >30
    int64_t n_mems_by_length_bin[6] = {0, 0, 0, 0, 0, 0};
    int64_t n_hits_by_length_bin[6] = {0, 0, 0, 0, 0, 0};

    // ---- clusters / local alignment (Q4) ----
    int64_t n_clusters = 0;
    int64_t n_cluster_graphs = 0;
    int64_t cluster_graph_nodes_total = 0;
    int64_t cluster_graph_edges_total = 0;
    int64_t largest_cluster_graph_nodes = 0;
    int64_t largest_cluster_graph_edges = 0;
    int64_t best_cluster_read_coverage = 0;
    int64_t n_local_multipath_alignments = 0;
    int64_t n_alt_mappings_before_cap = 0;
    int64_t n_alt_mappings_after_cap = 0;

    // ---- splice-search aggregates (Q2) ----
    int64_t n_splice_anchors_considered = 0;
    int64_t n_anchors_gate_opened = 0;
    int64_t n_anchors_gate_skipped = 0;   // soft-clip gate did not fire
    int64_t n_adapter_rejected = 0;
    bool splice_improved_best_alignment = false;
    bool splice_changed_primary = false;

    // ---- soft-clip / splice gate configuration (Q2) ----
    // Constant for a given run, but echoed on every record so each row is
    // self-describing: it documents the exact thresholds the gate/rescue metrics
    // above were measured against (they change with scoring params / read length).
    int64_t min_softclipped_score_for_splice = 0; // min tail score to open the gate
    int64_t min_softclip_length_for_splice = 0;   // min soft-clip length to rescue
    int64_t max_softclip_overlap = 0;             // allowed anchor/candidate overlap

    // ---- proactive bounded-MEM-window proxy (Q3) ----
    int64_t n_proactive_intervals = 0;
    int64_t n_proactive_intervals_with_raw_mem_evidence = 0;
    int64_t n_proactive_raw_mem_candidates = 0;
    int64_t n_proactive_candidate_windows_proxy = 0;
    int64_t n_proactive_candidates_that_current_gate_would_skip = 0;
    int64_t n_proactive_candidates_that_current_rescue_would_not_see = 0;
    double proactive_extra_candidate_ratio = 0.0;

    // ---- phase timers (microseconds) (Q4) ----
    int64_t time_find_mems_usec = 0;
    int64_t time_cluster_usec = 0;
    int64_t time_query_cluster_graphs_usec = 0;
    int64_t time_align_cluster_graphs_usec = 0;
    int64_t time_splice_rescue_usec = 0;
    int64_t time_proactive_proxy_usec = 0;
    int64_t time_total_usec = 0;
    int64_t elapsed_usec_total = 0; // alias of time_total_usec, for convenience

    // ---- experimental STAR-style MMP seed generator (Q1/Q5; --mmp-seed) ----
    // Populated only when the MMP generator is enabled; all zero otherwise.
    int64_t n_mmp_seeds_generated = 0;   // breakpoint-pinned MMP seeds surfaced this read
    int64_t n_mmp_seed_hits = 0;         // total located graph hits those seeds added
    int64_t n_mmp_seeds_new = 0;         // MMP seeds NOT already surfaced by the raw-MEM path
    int64_t n_mmp_seeds_kept_short = 0;  // MMP seeds below min_softclip_length_for_splice
                                         // (the length the raw-MEM path drops -> the recovery target)
    int64_t n_mmp_rc_seeds = 0;          // reverse-complement (breakpoint-pinned) right-tail seeds
    int64_t n_mmp_chain_seeds = 0;       // seeds beyond the first in a sequential MMP chain
    int64_t time_mmp_seed_usec = 0;

    // ---- placement + nested detail ----
    std::vector<TracePosition> final_positions; // primary alignment, for truth join
    std::vector<SpliceAnchorTrace> anchors;
    std::vector<ProactiveIntervalTrace> proactive_intervals;
    std::vector<MemDetail> mem_details;          // only when truth file supplied
};

// ---- sink control (called once, single-threaded, from mpmap_main) ----

/// Open the trace output file. After this, enabled() returns true. Writes a
/// leading header record describing the schema. On failure, tracing stays disabled
/// (caller may check enabled()).
void open(const std::string& path);

/// Load an optional truth-junction TSV (chrom donor acceptor strand junction_id).
/// Enables emission of per-MEM detail so truth overlap can be joined externally.
void open_truth(const std::string& path);

/// Flush and close the trace file. Safe to call when never opened.
void close();

/// True if tracing is active. One relaxed boolean load; safe on the hot path.
bool enabled();

/// True if a truth-junction file was loaded (drives per-MEM detail emission).
bool truth_loaded();

// ---- per-read record routing ----

/// Thread-local pointer to the record currently being filled on this thread, or
/// nullptr. The shared splice helpers consult this to attribute their metrics.
SpliceSearchTrace*& current();

/// Serialize one record as a single JSON line (thread-safe).
void write(const SpliceSearchTrace& rec);

/// RAII: point current() at `rec` for this thread, restore on scope exit.
struct TraceGuard {
    SpliceSearchTrace* prev;
    explicit TraceGuard(SpliceSearchTrace* rec) : prev(current()) { current() = rec; }
    ~TraceGuard() { current() = prev; }
    TraceGuard(const TraceGuard&) = delete;
    TraceGuard& operator=(const TraceGuard&) = delete;
};

/// RAII scoped timer that accumulates elapsed microseconds into `acc`, but only
/// when `active` (so disabled tracing never even reads the clock).
struct ScopedTimer {
    int64_t* acc = nullptr;
    std::chrono::steady_clock::time_point t0;
    ScopedTimer(int64_t& a, bool active) {
        if (active) {
            acc = &a;
            t0 = std::chrono::steady_clock::now();
        }
    }
    ~ScopedTimer() {
        if (acc) {
            *acc += std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
        }
    }
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
};

/// Candidate-generation-only proxy for a proactive bounded-MEM-window front end.
///
/// Given the primary alignment's soft-clipped tails and the raw MEM pool, this
/// counts how many raw MEM hits a proactive stage would surface in each unexplained
/// tail interval -- WITHOUT applying the current soft-clip score gate -- and how
/// many of those the current rescue would never see. It performs only read-coordinate
/// arithmetic over `mems` (no graph or index queries) and never mutates mapping
/// output. The intron-distance / co-linearity constraint is intentionally NOT
/// applied here, so the counts are a conservative upper bound (see the note in the
/// .cpp). Results are written into `tr`.
///
/// \param mems                        raw MEM pool for the read
/// \param anchor_mem_ptrs             MEM* already claimed by the winning cluster
/// \param read_len                    read length
/// \param aligned_begin,aligned_end   primary alignment's aligned read interval
/// \param left_tail_max_score,right_tail_max_score  best possible tail scores
/// \param min_softclip_length_for_splice  length threshold (mirrors rescue)
/// \param max_softclip_overlap        overlap slack (mirrors rescue)
/// \param min_softclipped_score_for_splice  the current gate threshold
/// \param current_rescue_candidates   candidates the real rescue considered
void fill_proactive(const std::vector<MaximalExactMatch>& mems,
                    const std::unordered_map<const MaximalExactMatch*, bool>& anchor_mem_ptrs,
                    std::string::const_iterator seq_begin,
                    int64_t read_len,
                    int64_t aligned_begin, int64_t aligned_end,
                    int64_t left_tail_max_score, int64_t right_tail_max_score,
                    int64_t min_softclip_length_for_splice,
                    int64_t max_softclip_overlap,
                    int64_t min_softclipped_score_for_splice,
                    int64_t current_rescue_candidates,
                    SpliceSearchTrace& tr);

/// Bucket a MEM length into one of the 6 histogram bins (<8,8-11,12-15,16-20,21-30,>30).
inline int length_bin(int64_t len) {
    if (len < 8) return 0;
    if (len < 12) return 1;
    if (len < 16) return 2;
    if (len < 21) return 3;
    if (len < 31) return 4;
    return 5;
}

} // namespace mpmap_trace
} // namespace vg

#endif
