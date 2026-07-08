#ifndef VG_MPMAP_MMP_HPP_INCLUDED
#define VG_MPMAP_MMP_HPP_INCLUDED

/**
 * \file mpmap_mmp.hpp
 *
 * Experimental STAR-style Maximal Mappable Prefix (MMP) seed generation for vg mpmap.
 *
 * PURPOSE
 * -------
 * The current mpmap splice rescue surfaces splice-partner candidates from the raw MEM
 * pool. MEM finding discards short and high-frequency exact matches by policy, so a
 * short exonic overhang past a junction may never seed the partner exon. STAR avoids
 * this by RE-SEEDING a fresh maximal-mappable-prefix search exactly at the breakpoint,
 * which pins a positioned seed on the partner exon even when the overhang is short.
 *
 * This layer adds that breakpoint-pinned seeding as an OPTIONAL, flag-gated candidate
 * generator that feeds mpmap's existing splice-rescue scoring. It is disabled unless
 * mpmap_mmp::configure() turns it on (via --mmp-seed); when disabled the only cost is a
 * single boolean branch at the one call site. It follows the non-invasive pattern of the
 * trace layer: a free function + file-static config, so multipath_mapper.hpp is untouched.
 *
 * NOTE
 * ----
 * Unlike the pure-observation trace layer, this DOES change candidate generation when
 * enabled. "Non-invasive" here means "isolated behind a default-off gate and confined to
 * one new TU plus one gated call block", not "zero behavioral effect".
 */

#include <cstdint>
#include <vector>
#include <utility>

#include "mem.hpp"                   // MaximalExactMatch, gcsa::GCSA/LCPArray, pos_t
#include "snarl_distance_index.hpp"  // SnarlDistanceIndex, minimum_distance
#include "mem_accelerator.hpp"       // MEMAccelerator

namespace handlegraph { class PathPositionHandleGraph; }

namespace vg {

class Alignment;

namespace mpmap_mmp {

/// Runtime configuration for the MMP seed generator. All fields default to "off" or to
/// the mapper's existing defaults, so an un-configured generator never fires.
struct MmpParams {
    bool enabled = false;
    int64_t min_prefix = 12;                     // minimum MMP length kept as a seed
    int64_t overlap_tol = 8;                     // breakpoint overlap slack (= max_softclip_overlap)
    int64_t min_intron = 20;                     // distance-prune lower bound
    int64_t max_intron = (int64_t(1) << 18);     // distance-prune upper bound (= max_intron_length)
    int64_t hit_max = 16;                        // per-seed locate() cap
    int strand_mode = 0;                         // 0 = native (both tails), 1 = rc, 2 = both
    int64_t relax_accept = 0;                    // if >0, relaxed min soft-clip length applied
                                                 // to MMP-sourced splice candidates at acceptance
                                                 // (M7: converts short-overhang visibility into
                                                 // accepted splices). 0 = no relaxation.
    bool chain = false;                          // sequential MMP chaining (STAR-style walk)
    int64_t max_seeds = 4;                       // max seeds per tail when chaining
    bool primary = false;                        // use MMP as a PRIMARY seeding source: a
                                                 // whole-read sequential MMP walk whose seeds
                                                 // augment the MEM pool BEFORE clustering (can
                                                 // change the mapping rate). Default off.
    int64_t primary_max_seeds = 16;              // cap on seeds per read for the primary walk
};

/// Set the configuration once, single-threaded, from mpmap_main (before the parallel
/// region). Mirrors mpmap_trace::open().
void configure(const MmpParams& params);

/// True if the MMP generator is enabled. One boolean load; safe on the hot path.
bool enabled();

/// True if MMP is configured as a primary seeding source (--mmp-primary).
bool primary_enabled();

/// Whole-read sequential MMP walk (STAR-style): append breakpoint-chained exact-match seeds,
/// with located graph hits, to `mems` so they augment the MEM pool before clustering. Unlike
/// the splice-rescue generator this produces MaximalExactMatch objects by value into `mems`
/// (which owns them). Used only when primary seeding is enabled. Returns the number appended.
size_t generate_primary_seeds(gcsa::GCSA* gcsa, const Alignment& alignment,
                              std::vector<MaximalExactMatch>& mems);

/// The active configuration (valid after configure()).
const MmpParams& params();

/// Clear the thread_local synthesized-MEM backing store. Call at the top of each read's
/// mapping so pointers from a previous read cannot dangle into this one.
void reset_read_store();

/// True if `mem` is one of the MMP seeds synthesized for the current read (i.e. it lives
/// in this thread's MMP store). Used by the splice-acceptance gate to apply the M7 relaxed
/// length threshold only to MMP-sourced candidates. Returns false when the generator is
/// disabled or `mem` is a raw MEM.
bool is_mmp_seed(const MaximalExactMatch* mem);

/// Generate STAR-style breakpoint-pinned MMP seeds for one soft-clipped tail of
/// `alignment` and append them (as (synthesized MEM*, graph pos)) to
/// `hit_candidates_out`, deduped against nothing here (caller dedups). The synthesized
/// MaximalExactMatch objects live in a pointer-stable thread_local store that stays valid
/// until reset_read_store(); callers MUST NOT retain the pointers past the current read.
///
/// \param search_left  true for a left soft-clip tail [0, primary_interval.first)
///                     (native backward search); false for a right tail.
void generate_mmp_seeds(gcsa::GCSA* gcsa, gcsa::LCPArray* lcp,
                        MEMAccelerator* accelerator,
                        SnarlDistanceIndex* distance_index,
                        handlegraph::PathPositionHandleGraph* xindex,
                        const Alignment& alignment,
                        const std::pair<int64_t, int64_t>& primary_interval,
                        bool search_left,
                        std::vector<std::pair<const MaximalExactMatch*, pos_t>>& hit_candidates_out);

} // namespace mpmap_mmp
} // namespace vg

#endif
