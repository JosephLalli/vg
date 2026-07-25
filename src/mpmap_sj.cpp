/**
 * \file mpmap_sj.cpp
 *
 * Implementation of the STAR first-pass splice-junction collector (item 7). Thread-safe
 * accumulation of discovered junctions and emission of an SJ.out.tab-style table.
 */

#include "mpmap_sj.hpp"

#include "position.hpp"                             // make_pos_t / pos_t
#include "handle.hpp"                               // PathPositionHandleGraph, path_handle_t, PathSense

#include <fstream>
#include <mutex>
#include <atomic>
#include <map>
#include <unordered_set>
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
static const handlegraph::PathPositionHandleGraph* g_graph = nullptr;  // for path projection

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

void set_graph(const handlegraph::PathPositionHandleGraph* graph) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_graph = graph;
}

// Classify a graph-native (node, offset, orientation) endpoint by embedded-path MEMBERSHIP (no
// proximity search). vg has no "gene" path sense, so genes are represented explicitly: `vg rna
// --add-tx-bodies` embeds one unspliced BODY path per distinct transcript intron structure under
// the reserved GENERIC name namespace "__txbody__|PROV|txid|gene|k" (PROV = REF if the transcript
// has a reference projection, else ALT). Classification is then pure set membership on the endpoint
// node:
//   * on a __txbody__ path  -> inside that gene. Provenance (ref_gene/nonref_gene) is the body's
//     PROV -- a construction-time property of the transcript, NOT the local node's assembly -- so a
//     read inside a reference gene's haplotype-specific intronic insert is still ref_gene. exon vs
//     intron: exon iff the SAME transcript's exon-chain path (<txid>_R*/_H*, embedded by vg rna
//     -r/-a) also covers the node, else intron.
//   * else on a REFERENCE assembly path -> ref_genomic (intergenic, on the reference).
//   * else on a HAPLOTYPE assembly path -> nonref_genomic (intergenic, non-reference only).
// ref_pos is the exact node-local linear coordinate on the reference assembly, or -1 when the
// endpoint is off the reference. Called only from close() (single-threaded, holding g_mutex).
struct EndpointClass { std::string path_name = "."; std::string cls = "none"; std::string region = "."; int64_t ref_pos = -1; };

static bool sj_starts_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}
// Strip a trailing vg-rna exon-chain copy suffix ("_R<n>" / "_H<n>") to recover the transcript id.
static std::string sj_exon_chain_base(const std::string& name) {
    auto us = name.rfind('_');
    if (us == std::string::npos || us + 2 > name.size()) return name;
    char c = name[us + 1];
    if (c != 'R' && c != 'H') return name;
    for (size_t i = us + 2; i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') return name;
    }
    return name.substr(0, us);
}
// Prefer a CHM13/reference-looking name, then the lexicographically-smallest, for stable output.
static bool sj_name_better(const std::string& cand, const std::string& cur, bool have) {
    if (!have) return true;
    bool cur_chm = cur.find("CHM13") != std::string::npos;
    bool new_chm = cand.find("CHM13") != std::string::npos;
    return (new_chm != cur_chm) ? new_chm : (cand < cur);
}

static EndpointClass classify_endpoint(int64_t node_id, int64_t offset, bool rev) {
    EndpointClass out;
    if (g_graph == nullptr) {
        return out;
    }
    static const std::string BODY_PREFIX = "__txbody__|";

    bool in_ref_gene = false, in_alt_gene = false;
    std::string ref_gene_id, alt_gene_id;
    std::vector<std::string> exon_chain_bases;   // transcript ids whose exon-chain covers this node
    bool on_ref_asm = false, on_hap_asm = false;
    std::string ref_name = ".", hap_name = ".";
    bool have_ref_name = false, have_hap_name = false;

    for (bool orient : {false, true}) {
        handle_t h = g_graph->get_handle(node_id, orient);
        g_graph->for_each_step_on_handle(h, [&](const step_handle_t& step) {
            path_handle_t ph = g_graph->get_path_handle_of_step(step);
            std::string name = g_graph->get_path_name(ph);
            if (sj_starts_with(name, BODY_PREFIX)) {
                // __txbody__|PROV|txid|gene|k
                std::vector<std::string> parts;
                std::stringstream ss(name);
                std::string tok;
                while (std::getline(ss, tok, '|')) parts.push_back(tok);
                if (parts.size() >= 3) {
                    const std::string& prov = parts[1];
                    const std::string& txid = parts[2];
                    if (prov == "REF") { in_ref_gene = true; if (ref_gene_id.empty()) ref_gene_id = txid; }
                    else               { in_alt_gene = true; if (alt_gene_id.empty()) alt_gene_id = txid; }
                }
                return;
            }
            PathSense sense = g_graph->get_sense(ph);
            if (sense == PathSense::GENERIC) {
                exon_chain_bases.push_back(sj_exon_chain_base(name));
                return;
            }
            if (sense == PathSense::REFERENCE) {
                on_ref_asm = true;
                // Exact node-local reference coordinate of the (node, offset, rev) base.
                handle_t step_h = g_graph->get_handle_of_step(step);
                size_t node_len = g_graph->get_length(step_h);
                size_t step_start = g_graph->get_position_of_step(step);
                size_t fwd_off = rev ? (node_len - 1 - (size_t) offset) : (size_t) offset;
                size_t within = g_graph->get_is_reverse(step_h) ? (node_len - 1 - fwd_off) : fwd_off;
                int64_t pos = (int64_t) (step_start + within);
                if (sj_name_better(name, ref_name, have_ref_name)) {
                    ref_name = name; have_ref_name = true; out.ref_pos = pos;
                }
            } else {  // PathSense::HAPLOTYPE
                on_hap_asm = true;
                if (sj_name_better(name, hap_name, have_hap_name)) { hap_name = name; have_hap_name = true; }
            }
        });
    }

    // priority: reference gene > non-reference gene > reference genomic > non-reference genomic.
    if (in_ref_gene || in_alt_gene) {
        bool is_ref = in_ref_gene;
        out.cls = is_ref ? "ref_gene" : "nonref_gene";
        out.path_name = is_ref ? ref_gene_id : alt_gene_id;
        bool exonic = false;
        for (const auto& b : exon_chain_bases) if (b == out.path_name) { exonic = true; break; }
        out.region = exonic ? "exon" : "intron";
    } else if (on_ref_asm) {
        out.cls = "ref_genomic"; out.path_name = ref_name;
    } else if (on_hap_asm) {
        out.cls = "nonref_genomic"; out.path_name = hap_name;
    }
    return out;
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
    //   donor_path donor_class donor_region donor_ref_pos  acceptor_path acceptor_class acceptor_region acceptor_ref_pos
    // where *_path is the covering gene (transcript id) or assembly path, *_class is one of
    // ref_gene/nonref_gene/ref_genomic/nonref_genomic/none, *_region is exon/intron/. (only meaningful
    // for gene classes), and *_ref_pos is the linear reference-assembly coordinate ("." off-reference).
    // See classify_endpoint: classification is embedded-path membership, not a proximity search.
    if (!g_path.empty()) {
        std::ofstream out(g_path);
        if (!out.is_open()) {
            std::cerr << "[vg mpmap] warning: could not open SJ output '" << g_path << "'" << std::endl;
        } else {
            out << "#donor_node\tdonor_offset\tdonor_strand\tacceptor_node\tacceptor_offset"
                   "\tacceptor_strand\tmotif\tannotated\tunique_reads\tmulti_reads\tmax_overhang"
                   "\tdonor_path\tdonor_class\tdonor_region\tdonor_ref_pos"
                   "\tacceptor_path\tacceptor_class\tacceptor_region\tacceptor_ref_pos\n";
            for (const auto& kv : g_junctions) {
                const JKey& k = kv.first;
                const JVal& v = kv.second;
                // outSJfilterCountUniqueMin analog (annotated junctions exempt, as in STAR)
                if (g_min_unique > 0 && !v.annotated && v.unique_reads < g_min_unique) {
                    continue;
                }
                // classify each endpoint by best-overlapping path (reference gene > non-reference gene
                // > reference genomic > non-reference genomic), keeping a reference-assembly coordinate
                // when the endpoint lies on the reference.
                auto dref = classify_endpoint(std::get<0>(k), std::get<1>(k), std::get<2>(k));
                auto aref = classify_endpoint(std::get<3>(k), std::get<4>(k), std::get<5>(k));
                std::string d_pos = dref.ref_pos < 0 ? "." : std::to_string(dref.ref_pos);
                std::string a_pos = aref.ref_pos < 0 ? "." : std::to_string(aref.ref_pos);
                out << std::get<0>(k) << '\t' << std::get<1>(k) << '\t' << (std::get<2>(k) ? '-' : '+')
                    << '\t' << std::get<3>(k) << '\t' << std::get<4>(k) << '\t' << (std::get<5>(k) ? '-' : '+')
                    << '\t' << (v.motif.empty() ? "." : v.motif)
                    << '\t' << (v.annotated ? 1 : 0)
                    << '\t' << v.unique_reads << '\t' << v.multi_reads << '\t' << v.max_overhang
                    << '\t' << dref.path_name << '\t' << dref.cls << '\t' << dref.region << '\t' << d_pos
                    << '\t' << aref.path_name << '\t' << aref.cls << '\t' << aref.region << '\t' << a_pos << '\n';
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
