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
#include "handle.hpp"       // PathPositionHandleGraph get_handle / get_length (RC relocation)
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

bool augment_enabled() {
    return g_params.augment;
}

bool primary_enabled() {
    return g_params.primary;
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

// Run one maximal-mappable backward-search over the query chars [q_begin, q_end) and, if
// it yields a match >= min_prefix, synthesize a seed and emit its located hits into `out`.
// Returns the matched length L (0 if nothing emitted).
//
// The query is either the forward read tail (rc=false) or its reverse complement
// (rc=true). The seed's FORWARD read interval is anchored at `anchor`:
//   - anchor_is_end=true  : read interval is [anchor - L, anchor)  (seed's right end fixed;
//                           left tail -> junction-pinned, right tail -> distal-pinned).
//   - anchor_is_end=false : read interval is [anchor, anchor + L)  (seed's left/breakpoint
//                           end fixed; used for the RC right tail).
// For rc=true the located positions are relocated onto the read's strand with
// reverse_base_pos (sub-option a); the exact offset within a multi-node match is recovered
// by the downstream subgraph re-alignment in query_cluster_graphs.
static int64_t run_mmp(gcsa::GCSA* gcsa, handlegraph::PathPositionHandleGraph* xindex,
                       std::string::const_iterator q_begin, std::string::const_iterator q_end,
                       std::string::const_iterator anchor, bool anchor_is_end, bool rc,
                       const MmpParams& p,
                       std::vector<std::pair<const MaximalExactMatch*, pos_t>>& out,
                       mpmap_trace::SpliceSearchTrace* tr) {
    if (q_end - q_begin < p.min_prefix) {
        return 0;
    }
    gcsa::range_type range = gcsa::range_type(0, gcsa->size() - 1);
    gcsa::range_type matched = range;
    std::string::const_iterator cur = q_end;
    while (cur > q_begin) {
        std::string::const_iterator nc = cur - 1;
        if ((int64_t)(q_end - nc) > (int64_t) gcsa->order()) {
            break; // do not extend past the GCSA2 index order: beyond it the match is not
                   // verifiable and could follow a walk the branching graph does not contain
        }
        if (*nc == 'N') {
            break;
        }
        gcsa::range_type next = gcsa->LF(range, gcsa->alpha.char2comp[(unsigned char) (*nc)]);
        if (gcsa::Range::empty(next)) {
            break;
        }
        range = next;
        matched = next;
        cur = nc;
    }
    int64_t L = q_end - cur;
    if (L < p.min_prefix || gcsa::Range::empty(matched)) {
        return 0;
    }

    std::string::const_iterator rb, re;
    if (anchor_is_end) {
        re = anchor;
        rb = re - L;
    } else {
        rb = anchor;
        re = rb + L;
    }

    g_mem_store.emplace_back(rb, re, matched);
    MaximalExactMatch& mem = g_mem_store.back();
    g_mem_ptrs.insert(&mem);
    mem.match_count = gcsa->count(matched);
    mem.queried_count = mem.match_count;
    mem.fragment = 0;
    mem.primary = true;
    if (p.hit_max > 0) {
        gcsa->locate(matched, p.hit_max, mem.nodes);
    } else {
        gcsa->locate(matched, mem.nodes);
    }
    const MaximalExactMatch* mp = &mem;

    int64_t new_hits = 0;
    for (gcsa::node_type node : mem.nodes) {
        pos_t pos = make_pos_t(node);
        if (rc) {
            size_t nlen = xindex->get_length(xindex->get_handle(id(pos)));
            pos = reverse_base_pos(pos, nlen);
        }
        bool dup = false;
        for (const auto& hc : out) {
            if (hc.second == pos && hc.first->begin == mp->begin && hc.first->end == mp->end) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            out.emplace_back(mp, pos);
            ++new_hits;
        }
    }
    if (tr != nullptr) {
        tr->n_mmp_seeds_generated += 1;
        tr->n_mmp_seed_hits += (int64_t) mem.nodes.size();
        if (new_hits > 0) {
            tr->n_mmp_seeds_new += 1;
        }
        if (L < tr->min_softclip_length_for_splice) {
            tr->n_mmp_seeds_kept_short += 1;
        }
        if (rc) {
            tr->n_mmp_rc_seeds += 1;
        }
    }
    return L;
}

// Emit a chain of sequential MMP seeds over the query [q_begin, q_end): the first seed is
// the maximal suffix ending at q_end; each subsequent seed re-seeds over the query prefix
// that remains after removing the matched suffix (STAR's sequential MMP walk). With
// chaining disabled this emits a single seed (identical to the pre-chain behavior). The
// forward read anchor advances by the matched length each step (toward the read start for
// forward tails; away from the breakpoint for the RC right tail).
static void emit_chain(gcsa::GCSA* gcsa, handlegraph::PathPositionHandleGraph* xindex,
                       std::string::const_iterator q_begin, std::string::const_iterator q_end,
                       std::string::const_iterator anchor, bool anchor_is_end, bool rc,
                       const MmpParams& p,
                       std::vector<std::pair<const MaximalExactMatch*, pos_t>>& out,
                       mpmap_trace::SpliceSearchTrace* tr) {
    int64_t max_seeds = p.chain ? (p.max_seeds > 0 ? p.max_seeds : 1) : 1;
    std::string::const_iterator qe = q_end;
    std::string::const_iterator anc = anchor;
    for (int64_t depth = 0; depth < max_seeds && (qe - q_begin) >= p.min_prefix; ++depth) {
        int64_t L = run_mmp(gcsa, xindex, q_begin, qe, anc, anchor_is_end, rc, p, out, tr);
        if (L == 0) {
            break;
        }
        if (depth > 0 && tr != nullptr) {
            tr->n_mmp_chain_seeds += 1; // seeds beyond the first
        }
        qe = qe - L;
        anc = anchor_is_end ? (anc - L) : (anc + L);
    }
}

void generate_mmp_seeds(gcsa::GCSA* gcsa, gcsa::LCPArray* lcp,
                        MEMAccelerator* accelerator,
                        SnarlDistanceIndex* distance_index,
                        handlegraph::PathPositionHandleGraph* xindex,
                        const Alignment& alignment,
                        const std::pair<int64_t, int64_t>& primary_interval,
                        bool search_left,
                        std::vector<std::pair<const MaximalExactMatch*, pos_t>>& hit_candidates_out) {
    // Distance pruning is deferred to the downstream test_splice_candidates (which already
    // prunes donor->acceptor pairs by minimum_distance), so the distance index is not needed
    // to SURFACE the seed. lcp/accelerator are unused: run_mmp does a plain backward search
    // from the full range (accelerate_mem_query only accelerates from the read start and is
    // a BaseMapper method a free function cannot call). xindex IS used for RC relocation.
    (void) lcp;
    (void) accelerator;
    (void) distance_index;
    if (gcsa == nullptr) {
        return;
    }
    const MmpParams& p = g_params;
    const std::string& seq = alignment.sequence();
    std::string::const_iterator seq_begin = seq.begin();

    // Measure the generator on the current read's trace record (if tracing is active).
    mpmap_trace::SpliceSearchTrace* tr = mpmap_trace::current();
    int64_t _timer_sink = 0;
    mpmap_trace::ScopedTimer _seed_timer(tr ? tr->time_mmp_seed_usec : _timer_sink,
                                         tr != nullptr);

    if (search_left) {
        // LEFT tail [0, primary_interval.first): forward backward-search whose suffix ends
        // AT the breakpoint, so the seed is junction-pinned (STAR's re-seed). make_pos_t
        // gives the seed's begin position directly, both strands, no relocation.
        std::string::const_iterator brk = seq_begin + primary_interval.first;
        emit_chain(gcsa, xindex, seq_begin, brk, brk, /*anchor_is_end=*/true, /*rc=*/false,
                   p, hit_candidates_out, tr);
        return;
    }

    // RIGHT tail [primary_interval.second, read_len). --mmp-strand-mode selects the method:
    //   0 native -> forward backward-search (distal-pinned: suffix ends at read_end)
    //   1 rc     -> reverse-complement (breakpoint-pinned; positions relocated, sub-option a)
    //   2 both   -> emit both
    std::string::const_iterator tail_begin = seq_begin + primary_interval.second;
    if (p.strand_mode == 0 || p.strand_mode == 2) {
        emit_chain(gcsa, xindex, tail_begin, seq.end(), seq.end(), /*anchor_is_end=*/true,
                   /*rc=*/false, p, hit_candidates_out, tr);
    }
    if (p.strand_mode == 1 || p.strand_mode == 2) {
        // Reverse-complement the tail so a backward search is breakpoint-pinned; the seed's
        // forward read interval begins at the breakpoint (tail_begin). rc_tail is a temporary
        // -- the synthesized MEM references the forward read iterators, not rc_tail.
        std::string rc_tail = reverse_complement(std::string(tail_begin, seq.end()));
        emit_chain(gcsa, xindex, rc_tail.begin(), rc_tail.end(), tail_begin,
                   /*anchor_is_end=*/false, /*rc=*/true, p, hit_candidates_out, tr);
    }
}

// ---------------------------------------------------------------------------
// Primary seeding: a whole-read sequential MMP walk that augments the MEM pool
// ---------------------------------------------------------------------------

size_t generate_primary_seeds(gcsa::GCSA* gcsa, const Alignment& alignment,
                              std::vector<MaximalExactMatch>& mems) {
    if (gcsa == nullptr) {
        return 0;
    }
    const MmpParams& p = g_params;
    const std::string& seq = alignment.sequence();
    std::string::const_iterator seq_begin = seq.begin();
    std::string::const_iterator qe = seq.end();
    int64_t cap = p.primary_max_seeds > 0 ? p.primary_max_seeds : 16;

    mpmap_trace::SpliceSearchTrace* tr = mpmap_trace::current();
    int64_t _sink = 0;
    mpmap_trace::ScopedTimer _timer(tr ? tr->time_mmp_seed_usec : _sink, tr != nullptr);

    size_t added = 0;
    for (int64_t d = 0; d < cap && (qe - seq_begin) >= p.min_prefix; ++d) {
        // maximal exact suffix ending at qe (STAR's sequential MMP, right-to-left)
        gcsa::range_type range = gcsa::range_type(0, gcsa->size() - 1);
        gcsa::range_type matched = range;
        std::string::const_iterator cur = qe;
        while (cur > seq_begin) {
            std::string::const_iterator nc = cur - 1;
            if ((int64_t)(qe - nc) > (int64_t) gcsa->order()) {
                break; // do not extend past the GCSA2 index order (see run_mmp)
            }
            if (*nc == 'N') {
                break;
            }
            gcsa::range_type next = gcsa->LF(range, gcsa->alpha.char2comp[(unsigned char) (*nc)]);
            if (gcsa::Range::empty(next)) {
                break;
            }
            range = next;
            matched = next;
            cur = nc;
        }
        int64_t L = qe - cur;
        if (L < p.min_prefix || gcsa::Range::empty(matched)) {
            // no long-enough match ending at qe (e.g. a mismatch at qe-1); skip the blocking
            // base and re-seed, mimicking STAR's advance past a mismatch.
            qe = qe - 1;
            continue;
        }
        mems.emplace_back(cur, qe, matched);
        MaximalExactMatch& mem = mems.back();
        mem.match_count = gcsa->count(matched);
        mem.queried_count = mem.match_count;
        mem.fragment = 0;
        mem.primary = true;
        if (p.hit_max > 0) {
            gcsa->locate(matched, p.hit_max, mem.nodes);
        } else {
            gcsa->locate(matched, mem.nodes);
        }
        ++added;
        qe = cur; // next seed covers [seq_begin, cur)
    }
    if (tr != nullptr) {
        tr->n_mmp_primary_seeds += (int64_t) added;
    }
    return added;
}

} // namespace mpmap_mmp
} // namespace vg
