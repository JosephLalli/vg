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
#include <vector>
#include <utility>
#include <sstream>
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
    // Per-read capture (--sj-reads only): (read_name, chosen splice score).
    std::vector<std::pair<std::string, double>> reads;
};

static std::atomic<bool> g_enabled{false};
static std::atomic<bool> g_reads_enabled{false};
static std::atomic<bool> g_cand_enabled{false};
static std::string g_path;
static std::string g_reads_path;
static std::string g_cand_path;
static std::mutex g_mutex;
static std::map<JKey, JVal> g_junctions;
static int64_t g_min_unique = 0;  // outSJfilterCountUniqueMin analog: drop non-annotated junctions
                                  // with fewer than this many unique reads (0 = off)
static std::vector<std::string> g_candidates;  // pre-formatted --sj-candidates rows

void open(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_path = path;
    g_junctions.clear();
    g_enabled.store(true, std::memory_order_relaxed);
}

void set_min_unique(int64_t m) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_min_unique = m;
}

void open_reads(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_reads_path = path;
    g_reads_enabled.store(true, std::memory_order_relaxed);
    // Per-read capture needs junction collection running even without --sj-out.
    g_enabled.store(true, std::memory_order_relaxed);
}

void open_candidates(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cand_path = path;
    g_candidates.clear();
    g_cand_enabled.store(true, std::memory_order_relaxed);
}

bool candidates_enabled() {
    return g_cand_enabled.load(std::memory_order_relaxed);
}

void record_candidate(const std::string& read_name,
                      int64_t donor_id, int64_t donor_offset, bool donor_rev,
                      int64_t acceptor_id, int64_t acceptor_offset, bool acceptor_rev,
                      const std::string& motif, double motif_score, double connect_score,
                      double intron_score, double net_score) {
    if (!g_cand_enabled.load(std::memory_order_relaxed)) {
        return;
    }
    std::ostringstream row;
    row << read_name
        << '\t' << donor_id << '\t' << donor_offset << '\t' << (donor_rev ? '-' : '+')
        << '\t' << acceptor_id << '\t' << acceptor_offset << '\t' << (acceptor_rev ? '-' : '+')
        << '\t' << (motif.empty() ? "." : motif)
        << '\t' << motif_score << '\t' << connect_score << '\t' << intron_score << '\t' << net_score;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_candidates.push_back(row.str());
}

bool enabled() {
    return g_enabled.load(std::memory_order_relaxed);
}

void record(int64_t donor_id, int64_t donor_offset, bool donor_rev,
            int64_t acceptor_id, int64_t acceptor_offset, bool acceptor_rev,
            const std::string& motif, bool annotated, int64_t overhang, double multiplicity,
            const std::string& read_name, double chosen_score, bool anchor_repetitive) {
    if (!enabled()) {
        return;
    }
    JKey key(donor_id, donor_offset, donor_rev, acceptor_id, acceptor_offset, acceptor_rev);
    std::lock_guard<std::mutex> lock(g_mutex);
    JVal& v = g_junctions[key];
    v.motif = motif;
    v.annotated = v.annotated || annotated;
    // graph-native winAnchorMultimapNmax: a junction whose anchor k-mer maps to many graph loci is
    // repeat/paralog-derived; its read is effectively multi-mapping even if the local cluster made
    // its multiplicity look ~1 (mpmap hit-caps repeat MEMs and never sees the paralog copies).
    if (multiplicity < 1.5 && !anchor_repetitive) {
        v.unique_reads += 1;
    } else {
        v.multi_reads += 1;
    }
    if (overhang > v.max_overhang) {
        v.max_overhang = overhang;
    }
    if (g_reads_enabled.load(std::memory_order_relaxed) && !read_name.empty()) {
        v.reads.emplace_back(read_name, chosen_score);
    }
}

void close() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_enabled.load(std::memory_order_relaxed)
        && !g_reads_enabled.load(std::memory_order_relaxed)
        && !g_cand_enabled.load(std::memory_order_relaxed)) {
        return;
    }
    // Graph-native SJ table (one row per junction), written when --sj-out was given. Columns:
    //   donor_node donor_offset donor_strand  acceptor_node acceptor_offset acceptor_strand
    //   motif  annotated(0/1)  unique_reads  multi_reads  max_overhang
    if (!g_path.empty()) {
        std::ofstream out(g_path);
        if (!out.is_open()) {
            std::cerr << "[vg mpmap] warning: could not open SJ output '" << g_path << "'" << std::endl;
        } else {
            out << "#donor_node\tdonor_offset\tdonor_strand\tacceptor_node\tacceptor_offset"
                   "\tacceptor_strand\tmotif\tannotated\tunique_reads\tmulti_reads\tmax_overhang\n";
            for (const auto& kv : g_junctions) {
                const JKey& k = kv.first;
                const JVal& v = kv.second;
                // outSJfilterCountUniqueMin analog (annotated junctions exempt, as in STAR)
                if (g_min_unique > 0 && !v.annotated && v.unique_reads < g_min_unique) {
                    continue;
                }
                out << std::get<0>(k) << '\t' << std::get<1>(k) << '\t' << (std::get<2>(k) ? '-' : '+')
                    << '\t' << std::get<3>(k) << '\t' << std::get<4>(k) << '\t' << (std::get<5>(k) ? '-' : '+')
                    << '\t' << (v.motif.empty() ? "." : v.motif)
                    << '\t' << (v.annotated ? 1 : 0)
                    << '\t' << v.unique_reads << '\t' << v.multi_reads << '\t' << v.max_overhang << '\n';
            }
            out.flush();
            out.close();
        }
    }
    // Companion per-read table (--sj-reads): one row per (junction, supporting read). Lets an
    // external join classify how another aligner handled each read behind a given junction.
    if (g_reads_enabled.load(std::memory_order_relaxed) && !g_reads_path.empty()) {
        std::ofstream rout(g_reads_path);
        if (!rout.is_open()) {
            std::cerr << "[vg mpmap] warning: could not open SJ reads output '" << g_reads_path << "'" << std::endl;
        } else {
            rout << "#donor_node\tdonor_offset\tdonor_strand\tacceptor_node\tacceptor_offset"
                    "\tacceptor_strand\tmotif\tread_name\tchosen_score\n";
            for (const auto& kv : g_junctions) {
                const JKey& k = kv.first;
                const JVal& v = kv.second;
                for (const auto& r : v.reads) {
                    rout << std::get<0>(k) << '\t' << std::get<1>(k) << '\t' << (std::get<2>(k) ? '-' : '+')
                         << '\t' << std::get<3>(k) << '\t' << std::get<4>(k) << '\t' << (std::get<5>(k) ? '-' : '+')
                         << '\t' << (v.motif.empty() ? "." : v.motif)
                         << '\t' << r.first << '\t' << r.second << '\n';
                }
            }
            rout.flush();
            rout.close();
        }
    }
    // Per-candidate-join dump (--sj-candidates): one row per gate-passing candidate join.
    if (g_cand_enabled.load(std::memory_order_relaxed) && !g_cand_path.empty()) {
        std::ofstream cout_(g_cand_path);
        if (!cout_.is_open()) {
            std::cerr << "[vg mpmap] warning: could not open SJ candidates output '" << g_cand_path << "'" << std::endl;
        } else {
            cout_ << "#read_name\tdonor_node\tdonor_offset\tdonor_strand\tacceptor_node\tacceptor_offset"
                     "\tacceptor_strand\tmotif\tmotif_score\tconnect_score\tintron_score\tnet_score\n";
            for (const auto& row : g_candidates) {
                cout_ << row << '\n';
            }
            cout_.flush();
            cout_.close();
        }
    }
    g_enabled.store(false, std::memory_order_relaxed);
    g_reads_enabled.store(false, std::memory_order_relaxed);
    g_cand_enabled.store(false, std::memory_order_relaxed);
}

} // namespace mpmap_sj
} // namespace vg
