/**
 * \file mpmap_sj.cpp
 *
 * Implementation of the STAR first-pass splice-junction collector (item 7). Thread-safe
 * accumulation of discovered junctions and emission of an SJ.out.tab-style table.
 */

#include "mpmap_sj.hpp"

#include <fstream>
#include <mutex>
#include <atomic>
#include <map>
#include <tuple>
#include <iostream>

namespace vg {
namespace mpmap_sj {

// key: donor (id, offset, rev), acceptor (id, offset, rev)
using JKey = std::tuple<int64_t, int64_t, bool, int64_t, int64_t, bool>;

struct JVal {
    std::string motif;
    bool annotated = false;
    int64_t unique_reads = 0;
    int64_t multi_reads = 0;
    int64_t max_overhang = 0;
};

static std::atomic<bool> g_enabled{false};
static std::string g_path;
static std::mutex g_mutex;
static std::map<JKey, JVal> g_junctions;

void open(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_path = path;
    g_junctions.clear();
    g_enabled.store(true, std::memory_order_relaxed);
}

bool enabled() {
    return g_enabled.load(std::memory_order_relaxed);
}

void record(int64_t donor_id, int64_t donor_offset, bool donor_rev,
            int64_t acceptor_id, int64_t acceptor_offset, bool acceptor_rev,
            const std::string& motif, bool annotated, int64_t overhang, double multiplicity) {
    if (!enabled()) {
        return;
    }
    JKey key(donor_id, donor_offset, donor_rev, acceptor_id, acceptor_offset, acceptor_rev);
    std::lock_guard<std::mutex> lock(g_mutex);
    JVal& v = g_junctions[key];
    v.motif = motif;
    v.annotated = v.annotated || annotated;
    if (multiplicity < 1.5) {
        v.unique_reads += 1;
    } else {
        v.multi_reads += 1;
    }
    if (overhang > v.max_overhang) {
        v.max_overhang = overhang;
    }
}

void close() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_enabled.load(std::memory_order_relaxed)) {
        return;
    }
    std::ofstream out(g_path);
    if (!out.is_open()) {
        std::cerr << "[vg mpmap] warning: could not open SJ output '" << g_path << "'" << std::endl;
        g_enabled.store(false, std::memory_order_relaxed);
        return;
    }
    // Graph-native SJ table (one row per junction). Columns:
    //   donor_node donor_offset donor_strand  acceptor_node acceptor_offset acceptor_strand
    //   motif  annotated(0/1)  unique_reads  multi_reads  max_overhang
    out << "#donor_node\tdonor_offset\tdonor_strand\tacceptor_node\tacceptor_offset"
           "\tacceptor_strand\tmotif\tannotated\tunique_reads\tmulti_reads\tmax_overhang\n";
    for (const auto& kv : g_junctions) {
        const JKey& k = kv.first;
        const JVal& v = kv.second;
        out << std::get<0>(k) << '\t' << std::get<1>(k) << '\t' << (std::get<2>(k) ? '-' : '+')
            << '\t' << std::get<3>(k) << '\t' << std::get<4>(k) << '\t' << (std::get<5>(k) ? '-' : '+')
            << '\t' << (v.motif.empty() ? "." : v.motif)
            << '\t' << (v.annotated ? 1 : 0)
            << '\t' << v.unique_reads << '\t' << v.multi_reads << '\t' << v.max_overhang << '\n';
    }
    out.flush();
    out.close();
    g_enabled.store(false, std::memory_order_relaxed);
}

} // namespace mpmap_sj
} // namespace vg
