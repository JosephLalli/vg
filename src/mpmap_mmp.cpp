/**
 * \file mpmap_mmp.cpp
 *
 * Implementation of the experimental STAR-style MMP seed generator. See mpmap_mmp.hpp
 * for the rationale. This file owns the file-static configuration and the thread_local
 * pointer-stable backing store for synthesized MEMs.
 *
 * Milestone status: M1 (scaffold). configure()/enabled()/params()/reset_read_store()
 * are live; generate_mmp_seeds() is a no-op placeholder so the gated call site and the
 * default-off isolation guarantee can be verified before the GCSA2 walk lands (M2).
 */

#include "mpmap_mmp.hpp"
#include "mpmap_trace.hpp"  // measure the generator on the same trace record
#include "position.hpp"     // make_pos_t
#include <vg/vg.pb.h>       // Alignment (full definition, for .sequence())

#include <deque>
#include <unordered_set>

namespace vg {
namespace mpmap_mmp {

// ---------------------------------------------------------------------------
// Configuration (file-static; set once, single-threaded, from mpmap_main)
// ---------------------------------------------------------------------------

static MmpParams g_params;

void configure(const MmpParams& params) {
    g_params = params;
}

bool enabled() {
    return g_params.enabled;
}

const MmpParams& params() {
    return g_params;
}

// ---------------------------------------------------------------------------
// Thread-local synthesized-MEM backing store
// ---------------------------------------------------------------------------
//
// hit_candidates carry a raw `const MaximalExactMatch*` that downstream rescue
// dereferences while building the candidate alignment (query_cluster_graphs). Seeds we
// synthesize here therefore must outlive the whole rescue for the current read. A deque
// is pointer-stable across push_back (unlike vector), so appended MEMs never move.

static thread_local std::deque<MaximalExactMatch> g_mem_store;
// Addresses of the seeds in g_mem_store, so the splice-acceptance gate can recognize an
// MMP-sourced candidate (M7 relaxed length threshold). Kept in sync with g_mem_store.
static thread_local std::unordered_set<const MaximalExactMatch*> g_mem_ptrs;

void reset_read_store() {
    g_mem_store.clear();
    g_mem_ptrs.clear();
}

bool is_mmp_seed(const MaximalExactMatch* mem) {
    return g_mem_ptrs.find(mem) != g_mem_ptrs.end();
}

// ---------------------------------------------------------------------------
// Seed generation
// ---------------------------------------------------------------------------

void generate_mmp_seeds(gcsa::GCSA* gcsa, gcsa::LCPArray* lcp,
                        MEMAccelerator* accelerator,
                        SnarlDistanceIndex* distance_index,
                        handlegraph::PathPositionHandleGraph* xindex,
                        const Alignment& alignment,
                        const std::pair<int64_t, int64_t>& primary_interval,
                        bool search_left,
                        std::vector<std::pair<const MaximalExactMatch*, pos_t>>& hit_candidates_out) {
    // Distance pruning is deferred to the downstream test_splice_candidates (which already
    // prunes donor->acceptor pairs by minimum_distance), so the anchor position / distance
    // index / graph are not needed to SURFACE the seed. lcp/accelerator are unused: we run
    // a plain backward search from the full range (accelerate_mem_query only accelerates
    // from the read start and is a BaseMapper method a free function cannot call).
    (void) lcp;
    (void) accelerator;
    (void) distance_index;
    (void) xindex;
    if (gcsa == nullptr) {
        return;
    }
    const MmpParams& p = g_params;
    if (p.strand_mode == 1) {
        // "rc"-only mode is reserved for a future breakpoint-pinned right-tail path; the
        // shipped right tail below is a forward (distal-pinned) search, so treat rc-only
        // as "skip" to keep the flag meaning honest.
        if (!search_left) {
            return;
        }
    }

    // GCSA2 is backward-search only, so both tails are found as the maximal exact SUFFIX
    // ending at `curr_end`, extending left toward `tail_begin`. make_pos_t on the located
    // node gives the position of the seed's first (leftmost) base on the correct strand,
    // so no manual strand math is needed.
    //  - LEFT tail  [0, primary_interval.first): the suffix ends AT the breakpoint, so the
    //    seed is pinned at the junction (the acceptor-exon seed) -- STAR's re-seed.
    //  - RIGHT tail [primary_interval.second, read_len): the suffix ends at read_end
    //    (distal-pinned). A breakpoint-pinned right seed would need reverse-complement
    //    querying with multi-node position relocation (locate returns only the match start);
    //    that refinement is documented and reserved behind --mmp-strand-mode.
    const std::string& seq = alignment.sequence();
    std::string::const_iterator seq_begin = seq.begin();
    std::string::const_iterator curr_end;
    std::string::const_iterator tail_begin;
    if (search_left) {
        int64_t break_off = primary_interval.first;
        if (break_off < p.min_prefix) {
            return; // left tail shorter than the minimum seed we would keep
        }
        curr_end = seq_begin + break_off;
        tail_begin = seq_begin;
    } else {
        int64_t tail_len = (int64_t) seq.size() - primary_interval.second;
        if (tail_len < p.min_prefix) {
            return; // right tail shorter than the minimum seed we would keep
        }
        curr_end = seq.end();
        tail_begin = seq_begin + primary_interval.second;
    }

    // Measure the generator on the current read's trace record (if tracing is active).
    mpmap_trace::SpliceSearchTrace* tr = mpmap_trace::current();
    int64_t _timer_sink = 0;
    mpmap_trace::ScopedTimer _seed_timer(tr ? tr->time_mmp_seed_usec : _timer_sink,
                                         tr != nullptr);

    gcsa::range_type range = gcsa::range_type(0, gcsa->size() - 1); // full BWT range
    gcsa::range_type matched_range = range;
    std::string::const_iterator cur = curr_end; // shrinks left as the exact match extends

    while (cur > tail_begin) {
        std::string::const_iterator next_char = cur - 1;
        if (*next_char == 'N') {
            break; // N is non-informative; do not extend through it
        }
        gcsa::range_type next = gcsa->LF(range, gcsa->alpha.char2comp[(unsigned char) (*next_char)]);
        if (gcsa::Range::empty(next)) {
            break; // cannot extend further: the match so far is maximal
        }
        range = next;
        matched_range = next;
        cur = next_char;
    }

    int64_t mmp_len = curr_end - cur;
    if (mmp_len < p.min_prefix || gcsa::Range::empty(matched_range)) {
        return;
    }

    // Synthesize the seed in the pointer-stable store; locate its (capped) graph hits.
    g_mem_store.emplace_back(cur, curr_end, matched_range);
    MaximalExactMatch& mem = g_mem_store.back();
    g_mem_ptrs.insert(&mem);  // mark as MMP-sourced for the M7 acceptance relaxation
    mem.match_count = gcsa->count(matched_range);
    mem.queried_count = mem.match_count;
    mem.fragment = 0;
    mem.primary = true;
    if (p.hit_max > 0) {
        gcsa->locate(matched_range, p.hit_max, mem.nodes);
    } else {
        gcsa->locate(matched_range, mem.nodes);
    }
    const MaximalExactMatch* mem_ptr = &mem;

    int64_t new_hits = 0;
    for (gcsa::node_type gcsa_node : mem.nodes) {
        pos_t pos = make_pos_t(gcsa_node);
        // Semantic dedup: skip a seed that duplicates a hit already surfaced (by the
        // raw-MEM path or an earlier MMP call) for the same read interval + graph position.
        bool dup = false;
        for (const auto& hc : hit_candidates_out) {
            if (hc.second == pos && hc.first->begin == mem_ptr->begin
                && hc.first->end == mem_ptr->end) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            hit_candidates_out.emplace_back(mem_ptr, pos);
            ++new_hits;
        }
    }

    if (tr != nullptr) {
        tr->n_mmp_seeds_generated += 1;
        tr->n_mmp_seed_hits += (int64_t) mem.nodes.size();
        if (new_hits > 0) {
            // this seed surfaced a graph placement the raw-MEM path did not
            tr->n_mmp_seeds_new += 1;
        }
        if (mmp_len < tr->min_softclip_length_for_splice) {
            // a seed shorter than the length the raw-MEM path drops: the recovery target
            tr->n_mmp_seeds_kept_short += 1;
        }
    }
}

} // namespace mpmap_mmp
} // namespace vg
