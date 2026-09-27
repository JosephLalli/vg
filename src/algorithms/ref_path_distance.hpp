/** \file
 * Measures the distance between two graph positions along the reference path
 * (approximated by the longest connecting path)
 */

#ifndef VG_ALGORITHMS_REF_PATH_DISTANCE_HPP_INCLUDED
#define VG_ALGORITHMS_REF_PATH_DISTANCE_HPP_INCLUDED

#include <structures/rank_pairing_heap.hpp>
#include <gbwt/gbwt.h>

#include "handle.hpp"
#include "position.hpp"

namespace vg {
namespace algorithms {

using namespace std;

/// Search the local region around two positions and return the longest distance between
/// them along any paths found during this search. Returns numeric_limits<int64_t>::max()
/// if no shared path is found.
int64_t ref_path_distance(const PathPositionHandleGraph* graph, const pos_t& pos_1, const pos_t& pos_2,
                          const unordered_set<path_handle_t>& ref_paths, int64_t max_search_dist);

/// Measure the distance from pos_1 to pos_2 along the threads of a bidirectional GBWT, in the
/// same coordinates as ref_path_distance. Up to max_threads threads through pos_1's node are
/// walked forward until they reach pos_2's node or pass max_dist. Returns the longest distance
/// among the threads that reach pos_2, or numeric_limits<int64_t>::max() if none do.
int64_t gbwt_path_distance(const gbwt::GBWT& index, const HandleGraph& graph,
                           const pos_t& pos_1, const pos_t& pos_2,
                           int64_t max_dist, size_t max_threads);

}

}

#endif // VG_ALGORITHMS_REF_PATH_DISTANCE_HPP_INCLUDED
