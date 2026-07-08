/**
 * \file mpmap_trace.cpp
 *
 * Implementation of the vg mpmap splice-search instrumentation. See
 * mpmap_trace.hpp for the rationale (measuring whether MEM-based seeding/rescue is
 * sufficient for STAR-like novel splice-junction discovery, or whether a STAR-like
 * seed/MMP front end is needed). This file owns the trace sink (a single output
 * file guarded by a mutex), the thread-local "current record" pointer, the JSON
 * serializer, optional truth-junction loading, and the proactive bounded-MEM-window
 * proxy. None of this changes mapping behavior.
 */

#include "mpmap_trace.hpp"
#include "mem.hpp"

#include <fstream>
#include <mutex>
#include <sstream>
#include <iostream>
#include <atomic>
#include <algorithm>

namespace vg {
namespace mpmap_trace {

// ---------------------------------------------------------------------------
// Sink state (file-static; configured once, single-threaded, from mpmap_main)
// ---------------------------------------------------------------------------

// Relaxed flag consulted on the hot path. Set true only after the stream opens.
static std::atomic<bool> g_enabled{false};
static std::ofstream g_out;
static std::mutex g_out_mutex;

// Truth junctions are stored but overlap is computed EXTERNALLY (graph->reference
// coordinate mapping is out of scope for this pass). We keep the parsed records so
// we can (a) report how many were loaded in the header line and (b) switch on the
// emission of per-MEM detail that external tooling needs for the join.
struct TruthJunction {
    std::string chrom;
    int64_t donor = 0;
    int64_t acceptor = 0;
    std::string strand;
    std::string junction_id;
};
static std::vector<TruthJunction> g_truth;
static bool g_truth_loaded = false;

// Each worker thread fills one record at a time; the shared splice helpers reach it
// through this pointer. thread_local is correct because each OpenMP worker maps
// whole reads sequentially.
SpliceSearchTrace*& current() {
    static thread_local SpliceSearchTrace* cur = nullptr;
    return cur;
}

bool enabled() {
    return g_enabled.load(std::memory_order_relaxed);
}

bool truth_loaded() {
    return g_truth_loaded;
}

// ---------------------------------------------------------------------------
// JSON serialization (manual, dependency-free, deterministic field order)
// ---------------------------------------------------------------------------

static void json_escape(std::ostream& o, const std::string& s) {
    o << '"';
    for (char c : s) {
        switch (c) {
            case '"':  o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\n': o << "\\n";  break;
            case '\r': o << "\\r";  break;
            case '\t': o << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    o << "\\u00" << hex[(c >> 4) & 0xF] << hex[c & 0xF];
                } else {
                    o << c;
                }
        }
    }
    o << '"';
}

// A NO_SCORE sentinel is emitted as JSON null so downstream tooling sees a missing
// value rather than a huge negative integer.
static void emit_score(std::ostream& o, int64_t v) {
    if (v == NO_SCORE) {
        o << "null";
    } else {
        o << v;
    }
}

static void emit_int_array(std::ostream& o, const int64_t* a, size_t n) {
    o << '[';
    for (size_t i = 0; i < n; ++i) {
        if (i) o << ',';
        o << a[i];
    }
    o << ']';
}

static void emit_positions(std::ostream& o, const std::vector<TracePosition>& ps) {
    o << '[';
    for (size_t i = 0; i < ps.size(); ++i) {
        if (i) o << ',';
        o << "{\"node_id\":" << ps[i].node_id
          << ",\"offset\":" << ps[i].offset
          << ",\"is_reverse\":" << (ps[i].is_reverse ? "true" : "false") << '}';
    }
    o << ']';
}

static void emit_anchor(std::ostream& o, const SpliceAnchorTrace& a) {
    o << '{'
      << "\"anchor_index\":" << a.anchor_index
      << ",\"aligned_interval_begin\":" << a.aligned_interval_begin
      << ",\"aligned_interval_end\":" << a.aligned_interval_end
      << ",\"left_tail_len\":" << a.left_tail_len
      << ",\"right_tail_len\":" << a.right_tail_len
      << ",\"left_tail_max_score\":" << a.left_tail_max_score
      << ",\"right_tail_max_score\":" << a.right_tail_max_score
      << ",\"search_left\":" << (a.search_left ? "true" : "false")
      << ",\"search_right\":" << (a.search_right ? "true" : "false")
      << ",\"adapter_rejected\":" << (a.adapter_rejected ? "true" : "false")
      << ",\"n_aligned_splice_candidates\":" << a.n_aligned_splice_candidates
      << ",\"n_unaligned_cluster_candidates\":" << a.n_unaligned_cluster_candidates
      << ",\"n_unclustered_raw_mem_hit_candidates\":" << a.n_unclustered_raw_mem_hit_candidates
      << ",\"n_total_splice_candidates\":" << a.n_total_splice_candidates
      << ",\"n_candidates_aligned\":" << a.n_candidates_aligned
      << ",\"n_candidates_rejected_before_alignment\":" << a.n_candidates_rejected_before_alignment
      << ",\"n_candidates_rejected_after_alignment\":" << a.n_candidates_rejected_after_alignment
      << ",\"n_motif_pairs_examined\":" << a.n_motif_pairs_examined
      << ",\"motif_pairs_capped\":" << (a.motif_pairs_capped ? "true" : "false")
      << ",\"n_splice_edges_considered\":" << a.n_splice_edges_considered
      << ",\"n_splice_alignments_produced\":" << a.n_splice_alignments_produced
      << ",\"best_splice_score\":"; emit_score(o, a.best_splice_score); o
      << ",\"did_splice\":" << (a.did_splice ? "true" : "false")
      << '}';
}

static void emit_proactive_interval(std::ostream& o, const ProactiveIntervalTrace& p) {
    o << '{'
      << "\"interval_begin\":" << p.interval_begin
      << ",\"interval_end\":" << p.interval_end
      << ",\"side\":" << p.side
      << ",\"anchor_id\":" << p.anchor_id
      << ",\"tail_max_score\":" << p.tail_max_score
      << ",\"candidate_mem_count\":" << p.candidate_mem_count
      << ",\"candidate_hit_count\":" << p.candidate_hit_count
      << ",\"best_candidate_mem_length\":" << p.best_candidate_mem_length
      << ",\"max_hit_count\":" << p.max_hit_count
      << ",\"current_gate_would_skip\":" << (p.current_gate_would_skip ? "true" : "false")
      << '}';
}

static void emit_mem_detail(std::ostream& o, const MemDetail& m) {
    o << '{'
      << "\"read_begin\":" << m.read_begin
      << ",\"read_end\":" << m.read_end
      << ",\"length\":" << m.length
      << ",\"match_count\":" << m.match_count
      << ",\"reported_hits\":" << m.reported_hits
      << ",\"primary\":" << (m.primary ? "true" : "false")
      << ",\"hit_positions\":"; emit_positions(o, m.hit_positions); o
      << '}';
}

void write(const SpliceSearchTrace& r) {
    if (!enabled()) {
        return;
    }
    std::ostringstream o;
    o << "{\"record_type\":\"read\""
      << ",\"read_name\":"; json_escape(o, r.read_name); o
      << ",\"read_length\":" << r.read_length
      << ",\"is_paired\":" << (r.is_paired ? "true" : "false")
      << ",\"mate\":" << r.mate
      << ",\"is_rna_mode\":" << (r.is_rna_mode ? "true" : "false")
      << ",\"do_spliced_alignment\":" << (r.do_spliced_alignment ? "true" : "false")
      << ",\"mapping_succeeded\":" << (r.mapping_succeeded ? "true" : "false")
      << ",\"best_score\":" << r.best_score
      << ",\"second_best_score\":"; emit_score(o, r.second_best_score); o
      << ",\"mapq\":" << r.mapq
      // seed / MEM visibility
      << ",\"n_mems_total\":" << r.n_mems_total
      << ",\"n_mem_hits_total\":" << r.n_mem_hits_total
      << ",\"n_mems_after_filtering\":" << r.n_mems_after_filtering
      << ",\"n_mem_hits_after_hit_max\":" << r.n_mem_hits_after_hit_max
      << ",\"n_mem_true_occurrences\":" << r.n_mem_true_occurrences
      << ",\"max_hits_per_mem\":" << r.max_hits_per_mem
      << ",\"sum_hits_for_short_mems\":" << r.sum_hits_for_short_mems
      << ",\"n_mems_hit_capped\":" << r.n_mems_hit_capped
      << ",\"n_high_fanout_mems\":" << r.n_high_fanout_mems
      << ",\"n_mems_by_length_bin\":"; emit_int_array(o, r.n_mems_by_length_bin, 6); o
      << ",\"n_hits_by_length_bin\":"; emit_int_array(o, r.n_hits_by_length_bin, 6); o
      // clusters / local alignment
      << ",\"n_clusters\":" << r.n_clusters
      << ",\"n_cluster_graphs\":" << r.n_cluster_graphs
      << ",\"cluster_graph_nodes_total\":" << r.cluster_graph_nodes_total
      << ",\"cluster_graph_edges_total\":" << r.cluster_graph_edges_total
      << ",\"largest_cluster_graph_nodes\":" << r.largest_cluster_graph_nodes
      << ",\"largest_cluster_graph_edges\":" << r.largest_cluster_graph_edges
      << ",\"best_cluster_read_coverage\":" << r.best_cluster_read_coverage
      << ",\"n_local_multipath_alignments\":" << r.n_local_multipath_alignments
      << ",\"n_alt_mappings_before_cap\":" << r.n_alt_mappings_before_cap
      << ",\"n_alt_mappings_after_cap\":" << r.n_alt_mappings_after_cap
      // splice-search aggregates
      << ",\"n_splice_anchors_considered\":" << r.n_splice_anchors_considered
      << ",\"n_anchors_gate_opened\":" << r.n_anchors_gate_opened
      << ",\"n_anchors_gate_skipped\":" << r.n_anchors_gate_skipped
      << ",\"n_adapter_rejected\":" << r.n_adapter_rejected
      << ",\"splice_improved_best_alignment\":" << (r.splice_improved_best_alignment ? "true" : "false")
      << ",\"splice_changed_primary\":" << (r.splice_changed_primary ? "true" : "false")
      // soft-clip / splice gate configuration (echoed per record)
      << ",\"min_softclipped_score_for_splice\":" << r.min_softclipped_score_for_splice
      << ",\"min_softclip_length_for_splice\":" << r.min_softclip_length_for_splice
      << ",\"max_softclip_overlap\":" << r.max_softclip_overlap
      // proactive proxy
      << ",\"n_proactive_intervals\":" << r.n_proactive_intervals
      << ",\"n_proactive_intervals_with_raw_mem_evidence\":" << r.n_proactive_intervals_with_raw_mem_evidence
      << ",\"n_proactive_raw_mem_candidates\":" << r.n_proactive_raw_mem_candidates
      << ",\"n_proactive_candidate_windows_proxy\":" << r.n_proactive_candidate_windows_proxy
      << ",\"n_proactive_candidates_that_current_gate_would_skip\":" << r.n_proactive_candidates_that_current_gate_would_skip
      << ",\"n_proactive_candidates_that_current_rescue_would_not_see\":" << r.n_proactive_candidates_that_current_rescue_would_not_see
      << ",\"proactive_extra_candidate_ratio\":" << r.proactive_extra_candidate_ratio
      // timers
      << ",\"time_find_mems_usec\":" << r.time_find_mems_usec
      << ",\"time_cluster_usec\":" << r.time_cluster_usec
      << ",\"time_query_cluster_graphs_usec\":" << r.time_query_cluster_graphs_usec
      << ",\"time_align_cluster_graphs_usec\":" << r.time_align_cluster_graphs_usec
      << ",\"time_splice_rescue_usec\":" << r.time_splice_rescue_usec
      << ",\"time_proactive_proxy_usec\":" << r.time_proactive_proxy_usec
      << ",\"time_total_usec\":" << r.time_total_usec
      << ",\"elapsed_usec_total\":" << r.elapsed_usec_total
      // experimental MMP seed generator
      << ",\"n_mmp_seeds_generated\":" << r.n_mmp_seeds_generated
      << ",\"n_mmp_seed_hits\":" << r.n_mmp_seed_hits
      << ",\"n_mmp_seeds_new\":" << r.n_mmp_seeds_new
      << ",\"n_mmp_seeds_kept_short\":" << r.n_mmp_seeds_kept_short
      << ",\"n_mmp_rc_seeds\":" << r.n_mmp_rc_seeds
      << ",\"n_mmp_chain_seeds\":" << r.n_mmp_chain_seeds
      << ",\"time_mmp_seed_usec\":" << r.time_mmp_seed_usec;

    // nested arrays
    o << ",\"final_positions\":"; emit_positions(o, r.final_positions);

    o << ",\"anchors\":[";
    for (size_t i = 0; i < r.anchors.size(); ++i) {
        if (i) o << ',';
        emit_anchor(o, r.anchors[i]);
    }
    o << ']';

    o << ",\"proactive_intervals\":[";
    for (size_t i = 0; i < r.proactive_intervals.size(); ++i) {
        if (i) o << ',';
        emit_proactive_interval(o, r.proactive_intervals[i]);
    }
    o << ']';

    if (!r.mem_details.empty()) {
        o << ",\"mem_details\":[";
        for (size_t i = 0; i < r.mem_details.size(); ++i) {
            if (i) o << ',';
            emit_mem_detail(o, r.mem_details[i]);
        }
        o << ']';
    }

    o << "}\n";

    const std::string line = o.str();
    std::lock_guard<std::mutex> lock(g_out_mutex);
    g_out << line;
}

// ---------------------------------------------------------------------------
// Open / close / truth loading
// ---------------------------------------------------------------------------

void open(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_out_mutex);
    g_out.open(path);
    if (!g_out.is_open()) {
        std::cerr << "[vg mpmap] warning: could not open splice trace file '" << path
                  << "'; tracing disabled" << std::endl;
        return;
    }
    // Leading header/schema record so the file is self-describing.
    g_out << "{\"record_type\":\"header\",\"schema\":\"mpmap_splice_search_trace\","
          << "\"schema_version\":2,"
          << "\"note\":\"one record per read or mate; for paired reads the "
             "splice-search fields describe the joint pair-level splice search and "
             "may be duplicated across mates. Line order is thread-interleaved; sort "
             "by (read_name,mate) or run with -t 1 for deterministic ordering.\","
          << "\"truth_junctions_loaded\":" << g_truth.size() << "}\n";
    g_enabled.store(true, std::memory_order_relaxed);
}

void open_truth(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "[vg mpmap] warning: could not open truth junction file '" << path
                  << "'" << std::endl;
        return;
    }
    // Format (whitespace- or tab-separated), '#' comment lines ignored:
    //   chrom  donor  acceptor  strand  junction_id
    // Truth overlap is not computed internally (graph->reference mapping is left to
    // external tooling that joins on final_positions / mem_details); we load the
    // file so its size is recorded and per-MEM detail is emitted. TODO: internal
    // overlap once a cheap graph->reference projection is available here.
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream ss(line);
        TruthJunction j;
        if (!(ss >> j.chrom >> j.donor >> j.acceptor >> j.strand)) {
            continue; // skip malformed / header rows
        }
        ss >> j.junction_id; // optional
        g_truth.push_back(std::move(j));
    }
    g_truth_loaded = !g_truth.empty();
}

void close() {
    std::lock_guard<std::mutex> lock(g_out_mutex);
    if (g_out.is_open()) {
        g_out.flush();
        g_out.close();
    }
    g_enabled.store(false, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Proactive bounded-MEM-window proxy
// ---------------------------------------------------------------------------
//
// This mirrors the per-MEM predicate in
// MultipathMapper::identify_unaligned_splice_candidates (mem long enough, small
// overlap with the aligned region, enough coverage inside the soft clip) but does
// NOT apply the anchor-level soft-clip SCORE gate. That is the whole point: it
// counts the raw MEM candidates a proactive stage would surface even where the
// current rescue's gate never fires. It touches only read-coordinate arithmetic
// over `mems` -- no graph or distance-index queries -- so it is cheap and cannot
// change mapping output. The intron-distance / co-linearity constraint is
// intentionally omitted, so counts are a conservative UPPER bound on what a
// distance-constrained proactive stage would generate.

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
                    SpliceSearchTrace& tr) {

    // Consider each soft-clipped tail of the primary alignment as an unexplained
    // interval: left = [0, aligned_begin), right = [aligned_end, read_len).
    struct Tail { int side; int64_t begin; int64_t end; int64_t max_score; };
    const Tail tails[2] = {
        {-1, 0,           aligned_begin, left_tail_max_score},
        {+1, aligned_end, read_len,      right_tail_max_score},
    };

    for (const Tail& t : tails) {
        const int64_t tail_len = t.end - t.begin;
        if (tail_len <= 0) {
            continue; // no soft clip on this side -> no unexplained interval
        }

        ProactiveIntervalTrace pit;
        pit.interval_begin = t.begin;
        pit.interval_end = t.end;
        pit.side = t.side;
        pit.anchor_id = 0; // primary alignment
        pit.tail_max_score = t.max_score;
        pit.current_gate_would_skip = (t.max_score < min_softclipped_score_for_splice);

        for (const auto& mem : mems) {
            const int64_t mlen = mem.length();
            if (mlen < min_softclip_length_for_splice) {
                continue;
            }
            if (anchor_mem_ptrs.count(&mem)) {
                continue; // already explained by the winning cluster
            }
            const int64_t mem_begin = mem.begin - seq_begin;
            const int64_t mem_end = mem.end - seq_begin;

            // Same overlap / in-clip-coverage test as identify_unaligned_splice_candidates.
            int64_t overlap, ind_cov;
            if (t.side < 0) { // left soft clip: interval is [0, aligned_begin)
                overlap = mem_end - aligned_begin;
                ind_cov = std::min<int64_t>(mem_end, aligned_begin) - mem_begin;
            } else {          // right soft clip: interval is [aligned_end, read_len)
                overlap = aligned_end - mem_begin;
                ind_cov = mem_end - std::max<int64_t>(mem_begin, aligned_end);
            }
            if (overlap >= max_softclip_overlap || ind_cov < min_softclip_length_for_splice) {
                continue;
            }

            // This MEM is a proactive candidate window for this tail.
            const int64_t hits = static_cast<int64_t>(mem.nodes.size());
            pit.candidate_mem_count += 1;
            pit.candidate_hit_count += hits;
            pit.best_candidate_mem_length = std::max<int64_t>(pit.best_candidate_mem_length, mlen);
            pit.max_hit_count = std::max<int64_t>(pit.max_hit_count, hits);
        }

        tr.n_proactive_intervals += 1;
        if (pit.candidate_mem_count > 0) {
            tr.n_proactive_intervals_with_raw_mem_evidence += 1;
        }
        tr.n_proactive_raw_mem_candidates += pit.candidate_hit_count;
        tr.n_proactive_candidate_windows_proxy += pit.candidate_mem_count;
        if (pit.current_gate_would_skip) {
            // Candidates in a tail whose soft-clip score gate would NOT fire are
            // exactly the ones the current rescue never generates.
            tr.n_proactive_candidates_that_current_gate_would_skip += pit.candidate_hit_count;
            tr.n_proactive_candidates_that_current_rescue_would_not_see += pit.candidate_hit_count;
        }
        tr.proactive_intervals.push_back(pit);
    }

    tr.proactive_extra_candidate_ratio =
        static_cast<double>(tr.n_proactive_raw_mem_candidates) /
        static_cast<double>(std::max<int64_t>(1, current_rescue_candidates));
}

} // namespace mpmap_trace
} // namespace vg
