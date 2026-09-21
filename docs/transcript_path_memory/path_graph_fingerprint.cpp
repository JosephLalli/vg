#include <bdsg/packed_graph.hpp>

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;
using namespace handlegraph;

namespace {

constexpr uint64_t NO_INDEX = numeric_limits<uint64_t>::max();
constexpr uint64_t DENSE_LOOKUP_SLACK = 1024;
constexpr size_t HASH_BUFFER_BYTES = 64 * 1024;

uint64_t checked_u64(size_t value, const char* what) {
    if (value > numeric_limits<uint64_t>::max()) {
        throw overflow_error(string(what) + " exceeds uint64_t");
    }
    return static_cast<uint64_t>(value);
}

size_t checked_size(uint64_t value, const char* what) {
    if (value > numeric_limits<size_t>::max()) {
        throw overflow_error(string(what) + " exceeds size_t");
    }
    return static_cast<size_t>(value);
}

class BufferedSha256 {
public:
    explicit BufferedSha256(const string& domain) {
        context_ = EVP_MD_CTX_new();
        if (context_ == nullptr || EVP_DigestInit_ex(context_, EVP_sha256(), nullptr) != 1) {
            throw runtime_error("cannot initialize SHA-256");
        }
        bytes(domain.data(), domain.size());
        u64(1); // fingerprint protocol version
    }

    ~BufferedSha256() {
        EVP_MD_CTX_free(context_);
    }

    BufferedSha256(const BufferedSha256&) = delete;
    BufferedSha256& operator=(const BufferedSha256&) = delete;

    void u64(uint64_t value) {
        array<unsigned char, 8> encoded{};
        for (size_t i = 0; i < encoded.size(); ++i) {
            encoded[i] = static_cast<unsigned char>(value >> (8 * i));
        }
        bytes(encoded.data(), encoded.size());
    }

    void string_value(const std::string& value) {
        u64(checked_u64(value.size(), "string length"));
        bytes(value.data(), value.size());
    }

    void bytes(const void* data, size_t count) {
        const auto* cursor = static_cast<const unsigned char*>(data);
        while (count != 0) {
            const size_t available = HASH_BUFFER_BYTES - buffer_.size();
            const size_t copied = min(available, count);
            buffer_.insert(buffer_.end(), cursor, cursor + copied);
            cursor += copied;
            count -= copied;
            if (buffer_.size() == HASH_BUFFER_BYTES) {
                flush();
            }
        }
    }

    std::string hex_digest() {
        flush();
        array<unsigned char, EVP_MAX_MD_SIZE> digest{};
        unsigned int length = 0;
        if (EVP_DigestFinal_ex(context_, digest.data(), &length) != 1) {
            throw runtime_error("cannot finalize SHA-256");
        }
        static constexpr char hex[] = "0123456789abcdef";
        string result;
        result.reserve(length * 2);
        for (size_t i = 0; i < length; ++i) {
            result.push_back(hex[digest[i] >> 4]);
            result.push_back(hex[digest[i] & 0x0f]);
        }
        return result;
    }

private:
    EVP_MD_CTX* context_ = nullptr;
    vector<unsigned char> buffer_;

    void flush() {
        if (!buffer_.empty()) {
            if (EVP_DigestUpdate(context_, buffer_.data(), buffer_.size()) != 1) {
                throw runtime_error("cannot update SHA-256");
            }
            buffer_.clear();
        }
    }
};

struct Anchor {
    uint64_t path_rank = 0;
    uint64_t step_rank = 0;
    bool reverse = false;
    bool set = false;
};

bool operator<(const Anchor& left, const Anchor& right) {
    return left.path_rank != right.path_rank ? left.path_rank < right.path_rank
         : left.step_rank < right.step_rank;
}

class NodeLookup {
public:
    explicit NodeLookup(const vector<nid_t>& ids) {
        if (ids.empty()) {
            return;
        }
        const nid_t minimum = *min_element(ids.begin(), ids.end());
        const nid_t maximum = *max_element(ids.begin(), ids.end());
        const uint64_t count = checked_u64(ids.size(), "node count");
        const uint64_t dense_limit = count <= (numeric_limits<uint64_t>::max() - DENSE_LOOKUP_SLACK) / 8
            ? count * 8 + DENSE_LOOKUP_SLACK : numeric_limits<uint64_t>::max();
        if (minimum >= 0 && maximum >= minimum) {
            const uint64_t range = static_cast<uint64_t>(maximum) - static_cast<uint64_t>(minimum);
            if (range <= dense_limit && range < numeric_limits<uint64_t>::max()) {
                minimum_ = minimum;
                dense_.assign(checked_size(range + 1, "dense node-ID range"), NO_INDEX);
                for (size_t i = 0; i < ids.size(); ++i) {
                    dense_[static_cast<uint64_t>(ids[i]) - static_cast<uint64_t>(minimum_)] = checked_u64(i, "node index");
                }
                return;
            }
        }
        sparse_.reserve(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) {
            if (!sparse_.emplace(ids[i], checked_u64(i, "node index")).second) {
                throw runtime_error("duplicate node ID while indexing graph");
            }
        }
    }

    uint64_t at(nid_t id) const {
        if (!dense_.empty()) {
            if (id < minimum_) {
                throw out_of_range("path step refers to unknown node ID");
            }
            const uint64_t offset = static_cast<uint64_t>(id) - static_cast<uint64_t>(minimum_);
            if (offset >= dense_.size() || dense_[offset] == NO_INDEX) {
                throw out_of_range("path step refers to unknown node ID");
            }
            return dense_[offset];
        }
        auto found = sparse_.find(id);
        if (found == sparse_.end()) {
            throw out_of_range("path step refers to unknown node ID");
        }
        return found->second;
    }

private:
    nid_t minimum_ = 0;
    vector<uint64_t> dense_;
    unordered_map<nid_t, uint64_t> sparse_;
};

struct CanonicalToken {
    uint64_t path_rank;
    uint64_t step_rank;
    bool reverse;
};

bool operator<(const CanonicalToken& left, const CanonicalToken& right) {
    if (left.path_rank != right.path_rank) return left.path_rank < right.path_rank;
    if (left.step_rank != right.step_rank) return left.step_rank < right.step_rank;
    return left.reverse < right.reverse;
}

struct CanonicalEdge {
    CanonicalToken left;
    CanonicalToken right;
};

bool operator<(const CanonicalEdge& left, const CanonicalEdge& right) {
    if (left.left < right.left) return true;
    if (right.left < left.left) return false;
    return left.right < right.right;
}

CanonicalEdge reverse_complement(const CanonicalEdge& edge) {
    return {{edge.right.path_rank, edge.right.step_rank, !edge.right.reverse},
            {edge.left.path_rank, edge.left.step_rank, !edge.left.reverse}};
}

CanonicalToken token_for(const bdsg::PackedGraph& graph, const handle_t& handle,
                         const NodeLookup& lookup, const vector<Anchor>& anchors) {
    const uint64_t index = lookup.at(graph.get_id(handle));
    const Anchor& anchor = anchors.at(checked_size(index, "node index"));
    if (!anchor.set) {
        throw runtime_error("edge uses a node not covered by a named path");
    }
    return {anchor.path_rank, anchor.step_rank, bool(graph.get_is_reverse(handle) ^ anchor.reverse)};
}

void hash_token(BufferedSha256& hash, const CanonicalToken& token) {
    hash.u64(token.path_rank);
    hash.u64(token.step_rank);
    hash.u64(token.reverse);
}

void fingerprint(const string& filename) {
    cerr << "[fingerprint] Loading PackedGraph" << endl;
    bdsg::PackedGraph graph;
    graph.deserialize(filename);

    cerr << "[fingerprint] Sorting " << graph.get_path_count() << " path names" << endl;
    vector<pair<string, path_handle_t>> paths;
    paths.reserve(graph.get_path_count());
    graph.for_each_path_handle([&](const path_handle_t& path) {
        paths.emplace_back(graph.get_path_name(path), path);
    });
    sort(paths.begin(), paths.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    for (size_t i = 1; i < paths.size(); ++i) {
        if (paths[i - 1].first == paths[i].first) {
            throw runtime_error("duplicate path name");
        }
    }

    vector<nid_t> node_ids;
    node_ids.reserve(graph.get_node_count());
    graph.for_each_handle([&](const handle_t& handle) {
        node_ids.push_back(graph.get_id(handle));
    });
    NodeLookup lookup(node_ids);
    vector<Anchor> anchors(node_ids.size());

    cerr << "[fingerprint] Finding canonical node anchors" << endl;
    for (size_t path_rank = 0; path_rank < paths.size(); ++path_rank) {
        if (path_rank != 0 && path_rank % 100000 == 0) {
            cerr << "[fingerprint] Anchors: " << path_rank << "/" << paths.size() << " paths" << endl;
        }
        uint64_t step_rank = 0;
        graph.for_each_step_in_path(paths[path_rank].second, [&](const step_handle_t& step) {
            const handle_t handle = graph.get_handle_of_step(step);
            const uint64_t index = lookup.at(graph.get_id(handle));
            Anchor& anchor = anchors.at(checked_size(index, "node index"));
            if (!anchor.set) {
                anchor = {checked_u64(path_rank, "path rank"), step_rank,
                          graph.get_is_reverse(handle), true};
            }
            if (step_rank == numeric_limits<uint64_t>::max()) {
                throw overflow_error("path step rank exceeds uint64_t");
            }
            ++step_rank;
        });
    }

    for (size_t i = 0; i < anchors.size(); ++i) {
        if (!anchors[i].set) {
            throw runtime_error("refusing graph with node not covered by a named path: " + to_string(node_ids[i]));
        }
    }

    vector<uint64_t> nodes_by_anchor(node_ids.size());
    for (size_t i = 0; i < nodes_by_anchor.size(); ++i) nodes_by_anchor[i] = checked_u64(i, "node index");
    sort(nodes_by_anchor.begin(), nodes_by_anchor.end(), [&](uint64_t left, uint64_t right) {
        return anchors[checked_size(left, "node index")] < anchors[checked_size(right, "node index")];
    });

    BufferedSha256 node_hash("vg.transcript_path_fingerprint.node");
    cerr << "[fingerprint] Hashing nodes and edges" << endl;
    node_hash.u64(checked_u64(nodes_by_anchor.size(), "node count"));
    for (uint64_t index : nodes_by_anchor) {
        const size_t local_index = checked_size(index, "node index");
        const Anchor& anchor = anchors[local_index];
        const handle_t handle = graph.get_handle(node_ids[local_index], anchor.reverse);
        const string sequence = graph.get_sequence(handle);
        node_hash.u64(anchor.path_rank);
        node_hash.u64(anchor.step_rank);
        node_hash.u64(checked_u64(sequence.size(), "node sequence length"));
        node_hash.bytes(sequence.data(), sequence.size());
    }

    vector<CanonicalEdge> edges;
    edges.reserve(graph.get_edge_count());
    graph.for_each_edge([&](const edge_t& edge) {
        CanonicalEdge transformed = {token_for(graph, edge.first, lookup, anchors),
                                     token_for(graph, edge.second, lookup, anchors)};
        const CanonicalEdge reversed = reverse_complement(transformed);
        if (reversed < transformed) transformed = reversed;
        edges.push_back(transformed);
    });
    sort(edges.begin(), edges.end());

    BufferedSha256 edge_hash("vg.transcript_path_fingerprint.edge");
    edge_hash.u64(checked_u64(edges.size(), "edge count"));
    for (const CanonicalEdge& edge : edges) {
        hash_token(edge_hash, edge.left);
        hash_token(edge_hash, edge.right);
    }

    BufferedSha256 path_hash("vg.transcript_path_fingerprint.path");
    path_hash.u64(checked_u64(paths.size(), "path count"));
    cerr << "[fingerprint] Hashing named walks" << endl;
    for (size_t path_rank = 0; path_rank < paths.size(); ++path_rank) {
        if (path_rank != 0 && path_rank % 100000 == 0) {
            cerr << "[fingerprint] Walks: " << path_rank << "/" << paths.size() << " paths" << endl;
        }
        const path_handle_t& path = paths[path_rank].second;
        path_hash.string_value(paths[path_rank].first);
        path_hash.u64(graph.get_is_circular(path));
        path_hash.u64(checked_u64(graph.get_step_count(path), "path step count"));
        graph.for_each_step_in_path(path, [&](const step_handle_t& step) {
            hash_token(path_hash, token_for(graph, graph.get_handle_of_step(step), lookup, anchors));
        });
    }

    cout << "{\"nodes\":" << node_ids.size()
         << ",\"edges\":" << edges.size()
         << ",\"paths\":" << paths.size()
         << ",\"node_sha256\":\"" << node_hash.hex_digest()
         << "\",\"edge_sha256\":\"" << edge_hash.hex_digest()
         << "\",\"path_sha256\":\"" << path_hash.hex_digest() << "\"}" << endl;
}

}

int main(int argc, char** argv) {
    if (argc != 2) {
        cerr << "usage: path_graph_fingerprint INPUT.pg" << endl;
        return 2;
    }
    try {
        fingerprint(argv[1]);
        return 0;
    }
    catch (const exception& error) {
        cerr << "path_graph_fingerprint: " << error.what() << endl;
        return 1;
    }
}
