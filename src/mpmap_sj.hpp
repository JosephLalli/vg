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

/// Enable per-read capture (debug; --sj-reads): additionally record, per junction, the supporting
/// read names and the chosen splice score, and write a companion long-format table on close().
/// Implies junction collection even when --sj-out was not given.
void open_reads(const std::string& path);

/// Enable per-candidate-join dump (debug; --sj-candidates): record EVERY gate-passing candidate
/// splice join considered per read, with its score components, to diagnose splice mis-placement.
/// Independent of --sj-out.
void open_candidates(const std::string& path);

/// True if per-candidate-join dumping is active. One relaxed boolean load.
bool candidates_enabled();

/// Record one gate-passing candidate join (thread-safe; --sj-candidates only).
void record_candidate(const std::string& read_name,
                      int64_t donor_id, int64_t donor_offset, bool donor_rev,
                      int64_t acceptor_id, int64_t acceptor_offset, bool acceptor_rev,
                      const std::string& motif, double motif_score, double connect_score,
                      double intron_score, double net_score);

/// True if junction collection is active. One relaxed boolean load.
bool enabled();

/// Record one accepted splice junction (thread-safe). donor = last exonic base before the
/// intron, acceptor = first exonic base after it, as graph positions. `annotated` = the junction
/// reuses an existing graph edge. `multiplicity` < ~1.5 counts as a unique read, else multi.
/// `read_name`/`chosen_score` are captured only under --sj-reads.
void record(int64_t donor_id, int64_t donor_offset, bool donor_rev,
            int64_t acceptor_id, int64_t acceptor_offset, bool acceptor_rev,
            const std::string& motif, bool annotated, int64_t overhang, double multiplicity,
            const std::string& read_name = std::string(), double chosen_score = 0.0);

/// Write the aggregated junction table and close. Safe to call when never opened.
void close();

} // namespace mpmap_sj
} // namespace vg

#endif
