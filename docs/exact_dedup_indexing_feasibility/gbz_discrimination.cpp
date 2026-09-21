// How much more discriminative is an edge (a junction) than a node?
// GBWT answers both directly: find(node).size() is the number of threads
// visiting a node; extend(state, next).size() is the number taking one
// specific branch out of it. The ratio bounds how much a junction-first
// index would narrow a candidate transcript set relative to node overlap.
#include <gbwtgraph/gbz.h>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>
#include <algorithm>
#include <string>

using namespace gbwtgraph;
using namespace handlegraph;
using clk = std::chrono::steady_clock;
static double secs(clk::time_point a, clk::time_point b) {
  return std::chrono::duration<double>(b - a).count();
}
template <typename T>
static double pct(std::vector<T>& v, double p) {
  if (v.empty()) return 0;
  size_t i = (size_t)(p * (v.size() - 1));
  return (double)v[i];
}

int main(int argc, char** argv) {
  if (argc < 2) { std::cerr << "usage: " << argv[0] << " FILE.gbz [n_nodes]\n"; return 2; }
  size_t n_nodes = (argc > 2) ? std::stoul(argv[2]) : 50000;

  GBZ gbz;
  auto t0 = clk::now();
  sdsl::simple_sds::load_from(gbz, argv[1]);
  auto t1 = clk::now();
  std::cout << "load_seconds\t" << secs(t0, t1) << "\n";

  std::vector<handle_t> handles;
  handles.reserve(gbz.graph.get_node_count());
  gbz.graph.for_each_handle([&](const handle_t& h) { handles.push_back(h); });
  std::mt19937_64 rng(20260920);
  std::shuffle(handles.begin(), handles.end(), rng);
  size_t take = std::min(n_nodes, handles.size());

  std::vector<size_t> node_mult, edge_mult, branch_count;
  std::vector<double> ratio;
  size_t covered = 0;

  auto t2 = clk::now();
  for (size_t i = 0; i < take; i++) {
    gbwt::node_type n = GBWTGraph::handle_to_node(handles[i]);
    gbwt::SearchState st = gbz.index.find(n);
    if (st.empty()) continue;
    covered++;
    node_mult.push_back(st.size());

    // Enumerate the branches out of this node and how many threads take each.
    size_t branches = 0, best = 0;
    gbz.graph.follow_edges(handles[i], false, [&](const handle_t& next) {
      gbwt::SearchState ext = gbz.index.extend(st, GBWTGraph::handle_to_node(next));
      if (!ext.empty()) {
        branches++;
        edge_mult.push_back(ext.size());
        if (ext.size() > best) best = ext.size();
      }
      return true;
    });
    branch_count.push_back(branches);
    if (branches > 0 && st.size() > 0) ratio.push_back((double)best / (double)st.size());
  }
  auto t3 = clk::now();

  std::sort(node_mult.begin(), node_mult.end());
  std::sort(edge_mult.begin(), edge_mult.end());
  std::sort(branch_count.begin(), branch_count.end());
  std::sort(ratio.begin(), ratio.end());

  auto mean = [](const std::vector<size_t>& v) {
    long double s = 0; for (size_t x : v) s += x; return v.empty() ? 0.0L : s / v.size();
  };

  std::cout << "nodes_sampled\t"      << take << "\n";
  std::cout << "nodes_with_threads\t" << covered << "\n";
  std::cout << "node_mult_mean\t"     << (double)mean(node_mult) << "\n";
  std::cout << "node_mult_median\t"   << pct(node_mult, 0.50) << "\n";
  std::cout << "node_mult_p95\t"      << pct(node_mult, 0.95) << "\n";
  std::cout << "node_mult_max\t"      << (node_mult.empty()?0:node_mult.back()) << "\n";
  std::cout << "edge_mult_mean\t"     << (double)mean(edge_mult) << "\n";
  std::cout << "edge_mult_median\t"   << pct(edge_mult, 0.50) << "\n";
  std::cout << "edge_mult_p95\t"      << pct(edge_mult, 0.95) << "\n";
  std::cout << "branches_mean\t"      << (double)mean(branch_count) << "\n";
  std::cout << "branches_p95\t"       << pct(branch_count, 0.95) << "\n";
  // best-branch / node ratio: 1.0 means the branch does not narrow at all
  std::cout << "bestbranch_over_node_median\t" << pct(ratio, 0.50) << "\n";
  std::cout << "bestbranch_over_node_p05\t"    << pct(ratio, 0.05) << "\n";
  std::cout << "find_extend_us_per_node\t"     << secs(t2,t3)*1e6/(double)take << "\n";
  return 0;
}
