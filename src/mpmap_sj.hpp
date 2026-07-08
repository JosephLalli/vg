#ifndef VG_MPMAP_SJ_HPP_INCLUDED
#define VG_MPMAP_SJ_HPP_INCLUDED

/**
 * \file mpmap_sj.hpp
 *
 * Splice-junction collector for the STAR first-pass (item 7). Aggregates the novel/annotated
 * splice junctions discovered during mapping and emits a STAR-`SJ.out.tab`-style table -- the
 * first-pass output that a future pass 2 would consume (insert edges + remap).
 *
 * Design mirrors the trace sink: free functions + a mutex-guarded accumulator, enabled only when
 * an output file is set (--sj-out). Junctions are keyed graph-natively on the donor/acceptor
 * (node_id, offset, orientation) so they are unambiguous and directly usable to insert edges.
 */

#include <cstdint>
#include <string>

namespace vg {
namespace mpmap_sj {

/// Enable collection and set the output path (called once, single-threaded, from mpmap_main).
void open(const std::string& path);

/// True if junction collection is active. One relaxed boolean load.
bool enabled();

/// Record one accepted splice junction (thread-safe). donor = last exonic base before the
/// intron, acceptor = first exonic base after it, as graph positions. `annotated` = the junction
/// reuses an existing graph edge. `multiplicity` < ~1.5 counts as a unique read, else multi.
void record(int64_t donor_id, int64_t donor_offset, bool donor_rev,
            int64_t acceptor_id, int64_t acceptor_offset, bool acceptor_rev,
            const std::string& motif, bool annotated, int64_t overhang, double multiplicity);

/// Write the aggregated junction table and close. Safe to call when never opened.
void close();

} // namespace mpmap_sj
} // namespace vg

#endif
