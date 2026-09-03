#include "source_sink_overlay.hpp"

#include <handlegraph/util.hpp>

#include <cstdint>
#include <limits>
#include <vector>

//#define debug

namespace vg {

using namespace std;
using namespace handlegraph;

namespace {

/**
 * Tracks component traversal without retaining every component's node set.
 *
 * Pangenome graph identifiers are normally dense. In that case one bit per
 * possible identifier is substantially smaller than an unordered_set entry
 * per node. Sparse identifier spaces use a hash set instead, avoiding an
 * allocation proportional to max_node_id().
 */
class ComponentVisited {
public:
    explicit ComponentVisited(const HandleGraph* graph) : first(0), span(0) {
        const size_t nodes = graph->get_node_count();
        if (nodes == 0) {
            return;
        }

        const id_t minimum = graph->min_node_id();
        const id_t maximum = graph->max_node_id();
        const __int128 wide_span = static_cast<__int128>(maximum) -
                                   static_cast<__int128>(minimum) + 1;
        const __int128 dense_limit = static_cast<__int128>(nodes) * 8;
        if (wide_span > 0 && wide_span <= dense_limit &&
            wide_span <= static_cast<__int128>(numeric_limits<size_t>::max())) {
            first = minimum;
            span = static_cast<size_t>(wide_span);
            bits.assign((span + 63) / 64, 0);
        }
    }

    /// Marks an ID and returns true exactly once for each graph node.
    bool mark(id_t id) {
        if (!bits.empty()) {
            const __int128 wide_offset = static_cast<__int128>(id) -
                                         static_cast<__int128>(first);
            if (wide_offset >= 0 && wide_offset < static_cast<__int128>(span)) {
                const size_t offset = static_cast<size_t>(wide_offset);
                const uint64_t mask = uint64_t(1) << (offset & 63);
                uint64_t& word = bits[offset >> 6];
                if (word & mask) {
                    return false;
                }
                word |= mask;
                return true;
            }
        }
        return sparse.insert(id).second;
    }

private:
    id_t first;
    size_t span;
    vector<uint64_t> bits;
    unordered_set<id_t> sparse;
};

}

SourceSinkOverlay::SourceSinkOverlay(const HandleGraph* backing, size_t length, id_t source_id, id_t sink_id,
    bool break_disconnected) : node_length(length), backing(backing), source_id(source_id), sink_id(sink_id) {
   
    // Both IDs or neither must be specified.
    assert((this->source_id == 0) == (this->sink_id == 0));
   
    if (this->source_id == 0 || this->sink_id == 0) {
        // We need to autodetect our source and sink IDs
        id_t backing_max_id = backing->get_node_count() > 0 ? backing->max_node_id() : 0;
        
        this->source_id = backing_max_id + 1;
        this->sink_id = this->source_id + 1;
    }
    
#ifdef debug
    cerr << "Make overlay for kmer size " << length << " with source " << this->source_id << " and sink " << this->sink_id << endl;
#endif
    
    // Discover tips one component at a time. The old implementation retained
    // both a global visited set and an unordered_set of every node in every
    // component. On a whole pangenome that transient could rival the graph
    // itself. We only need actual tips plus one representative for a tipless
    // component, so retain one traversal stack and one visited bit/set.
    ComponentVisited traversed(backing);
    backing->for_each_handle([&](const handle_t& initial) {
        const handle_t root = backing->forward(initial);
        if (!traversed.mark(backing->get_id(root))) {
            return;
        }

        vector<handle_t> stack(1, root);
        bool component_has_tip = false;
        while (!stack.empty()) {
            const handle_t here = stack.back();
            stack.pop_back();

            auto visit_neighbor = [&](const handle_t& neighbor) {
                const handle_t forward = backing->forward(neighbor);
                if (traversed.mark(backing->get_id(forward))) {
                    stack.push_back(forward);
                }
                return true;
            };

            size_t degree = 0;
            backing->follow_edges(here, false, [&](const handle_t& neighbor) {
                ++degree;
                return visit_neighbor(neighbor);
            });
            if (degree == 0) {
                // `here` reads out of this component in forward orientation.
                backing_tails.insert(here);
                component_has_tip = true;
            }

            degree = 0;
            backing->follow_edges(here, true, [&](const handle_t& neighbor) {
                ++degree;
                return visit_neighbor(neighbor);
            });
            if (degree == 0) {
                backing_heads.insert(here);
                component_has_tip = true;
            }
        }

        if (!component_has_tip && break_disconnected) {
            // As historically allowed, choose an arbitrary node in a tipless
            // component; using the traversal root makes that choice repeatable
            // for a backing graph with stable for_each_handle() order.
            backing_heads.insert(root);
            backing->follow_edges(root, true, [&](const handle_t& fake_tail) {
                backing_tails.insert(fake_tail);
            });
        }
    });
    
    
}

handle_t SourceSinkOverlay::get_source_handle() const {
    return source_fwd;
}

handle_t SourceSinkOverlay::get_sink_handle() const {
    return sink_fwd;
}

bool SourceSinkOverlay::has_node(id_t node_id) const {
    return backing->has_node(node_id);
}

handle_t SourceSinkOverlay::get_handle(const id_t& node_id, bool is_reverse) const {
    if (node_id == source_id) {
        // They asked for the source node
        return is_reverse ? source_rev : source_fwd;
    } else if (node_id == sink_id) {
        // They asked for the sink node
        return is_reverse ? sink_rev : sink_fwd;
    } else {
        // Otherwise they asked for something in the backing graph
        handle_t backing_handle = backing->get_handle(node_id, is_reverse);
        
        // Budge up to make room for the source and sink in each orientation
        return as_handle(as_integer(backing_handle) + 4);
    }
}

id_t SourceSinkOverlay::get_id(const handle_t& handle) const {
    if (handle == source_fwd || handle == source_rev) {
        return source_id;
    } else if (handle == sink_fwd || handle == sink_rev) {
        return sink_id;
    } else {
        return backing->get_id(to_backing(handle));
    }
}

bool SourceSinkOverlay::get_is_reverse(const handle_t& handle) const {
    if (handle == source_fwd || handle == sink_fwd) {
        return false;
    } else if (handle == source_rev || handle == sink_rev) {
        return true;
    } else {
        return backing->get_is_reverse(to_backing(handle));
    }
}

handle_t SourceSinkOverlay::flip(const handle_t& handle) const {
    if (is_ours(handle)) {
        // In our block of two handles, orientation is the low bit
        return as_handle(as_integer(handle) ^ 1);
    } else {
        // Make the backing graph flip it
        return from_backing(backing->flip(to_backing(handle)));
    }
}

size_t SourceSinkOverlay::get_length(const handle_t& handle) const {
    if (is_ours(handle)) {
        // Both our fake nodes are the same length
        return node_length;
    } else {
        return backing->get_length(to_backing(handle));
    }
}

string SourceSinkOverlay::get_sequence(const handle_t& handle) const {
    if (handle == source_fwd || handle == sink_rev) {
        // Reading into the graph is all '#'
        return string(node_length, '#');
    } else if (handle == source_rev || handle == sink_fwd) {
        // Reading out of the graph is all '$'
        return string(node_length, '$');
    } else {
        assert(!is_ours(handle));
        return backing->get_sequence(to_backing(handle));
    }
}

bool SourceSinkOverlay::follow_edges_impl(const handle_t& handle, bool go_left, const function<bool(const handle_t&)>& iteratee) const {
    if (is_ours(handle)) {
        // We only care about the right of the source and the left of the sink
        if ((handle == source_fwd && !go_left) || (handle == source_rev && go_left)) {
            // We want the right of the source (every head node in the backing graph)
            // Make sure to put it in the appropriate orientation.
            
            for (const handle_t& backing_head : backing_heads) {
                // Feed each backing graph head to the iteratee, in the
                // appropriate orientation depending on which way we want to
                // go.
                if (!iteratee(from_backing(go_left ? backing->flip(backing_head) : backing_head))) {
                    // If they say to stop, stop
                    return false;
                }
            }
            
        } else if ((handle == sink_fwd && go_left) || (handle == sink_rev && !go_left)) {
            // We want the left of the sink (every tail node in the backing graph)
            // Make sure to put it in the appropriate orientation.
            
            for (const handle_t& backing_tail : backing_tails) {
                // Feed each backing graph tail to the iteratee, in the
                // appropriate orientation depending on which way we want to
                // go.
                if (!iteratee(from_backing(go_left ? backing_tail : backing->flip(backing_tail)))) {
                    // If they say to stop, stop
                    return false;
                }
            }
        }
        return true;
    } else {
        // The handle refers to a node in the backing graph
        auto backing_handle = to_backing(handle);
        
        if ((backing_heads.count(backing_handle) && go_left) || (backing_heads.count(backing->flip(backing_handle)) && !go_left)) {
            // We want to read left off a head (possibly in reverse) into the synthetic source
            if (!iteratee(go_left ? source_fwd : source_rev)) {
                // If they say stop, stop
                return false;
            }
        }
        
        if ((backing_tails.count(backing_handle) && !go_left) || (backing_tails.count(backing->flip(backing_handle)) && go_left)) {
            // We want to read right off a tail (possibly in reverse) into the synthetic sink
            if (!iteratee(go_left ? sink_rev : sink_fwd)) {
                // If they say stop, stop
                return false;
            }
        }
    
        // If we get through those, do the actual edges in the backing graph
        return backing->follow_edges(backing_handle, go_left, [&](const handle_t& found) -> bool {
            return iteratee(from_backing(found));
        });
    }
}

bool SourceSinkOverlay::for_each_handle_impl(const function<bool(const handle_t&)>& iteratee, bool parallel) const {
    
    // First do the sourece and sink we added
    if (!iteratee(source_fwd)) {
        return false;
    }
    if (!iteratee(sink_fwd)) {
        return false;
    }
    
#ifdef debug
    cerr << "Try backing graph " << (parallel ? "in parallel" : "") << endl;
#endif
    return backing->for_each_handle([&](const handle_t& backing_handle) -> bool {
        // Now do each backing node, possibly in parallel.
#ifdef debug
        cerr << "Invoke iteratee on " << backing->get_id(backing_handle) << endl;
#endif
        return iteratee(from_backing(backing_handle));
    }, parallel);
}

size_t SourceSinkOverlay::get_node_count() const {
    return backing->get_node_count() + 2;
}

id_t SourceSinkOverlay::min_node_id() const {
    return min(backing->min_node_id(), min(source_id, sink_id));
}
    
id_t SourceSinkOverlay::max_node_id() const {
    return max(backing->max_node_id(), max(source_id, sink_id));
}

size_t SourceSinkOverlay::get_degree(const handle_t& handle, bool go_left) const {
    if (is_ours(handle)) {
        if ((handle == source_fwd && !go_left) || (handle == source_rev && go_left)) {
            // We are reading into every graph head
            return backing_heads.size();
        } else if ((handle == sink_fwd && go_left) || (handle == sink_rev && !go_left)) {
            // We are reading into every graph tail
            return backing_tails.size();
        }
        // Otherwise we're reading off the outside ends of the source/sink nodes
        return 0;
    } else {
        // We need to find the backing graph degree and possibly adjust it if this is a head or tail
        handle_t backing_handle = to_backing(handle);
        
        size_t degree = backing->get_degree(backing_handle, go_left);
        
        if (backing_heads.count(backing->forward(backing_handle))) {
            // We are a head. Are we going off the left end when forward, or the right end when reverse?
            if (go_left != backing->get_is_reverse(backing_handle)) {
                // If so we count the synthetic edge.
                degree++;
            }
        }
        if (backing_tails.count(backing->forward(backing_handle))) {
            // We are a tial. Are we going off the left end when reverse, or the right end when forward?
            if (go_left != !backing->get_is_reverse(backing_handle)) {
                // If so we count the synthetic edge.
                degree++;
            }
        }
        
        return degree;
    }
}

handle_t SourceSinkOverlay::get_underlying_handle(const handle_t& handle) const {
    if (is_ours(handle)) {
        throw std::runtime_error("error:[SourceSinkOverlay] cannot request underlying handle of source or sink node");
    }
    return to_backing(handle);
}

}
