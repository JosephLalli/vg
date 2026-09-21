// Measures, on a GBZ built from an HST guide GBWT:
//   1. per-node thread multiplicity (how many transcript threads visit a node)
//   2. cost of resolving a step to a path identity (GBWT locate)
//   3. cost of recovering a step's offset by walking the thread from its start
// These are the three quantities that decide whether a GBZ can replace an XG
// for the panCollapse node -> transcript-set query.
#include <gbwtgraph/gbz.h>
#include <handlegraph/handle_graph.hpp>
#include <chrono>
#include <cstdint>
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
static size_t pct(const std::vector<size_t>& v, double p) {
  if (v.empty()) return 0;
  size_t i = (size_t)(p * (v.size() - 1));
  return v[i];
}

int main(int argc, char** argv) {
  if (argc < 2) { std::cerr << "usage: " << argv[0] << " FILE.gbz [n_nodes] [n_locate]\n"; return 2; }
  size_t n_nodes  = (argc > 2) ? std::stoul(argv[2]) : 20000;
  size_t n_locate = (argc > 3) ? std::stoul(argv[3]) : 2000;

  GBZ gbz;
  auto t0 = clk::now();
  sdsl::simple_sds::load_from(gbz, argv[1]);
  auto t1 = clk::now();
  std::cout << "load_seconds\t" << secs(t0, t1) << "\n";
  std::cout << "nodes\t"  << gbz.graph.get_node_count() << "\n";
  std::cout << "paths\t"  << gbz.graph.get_path_count() << "\n";

  // Enumerate node handles once so we can sample uniformly.
  std::vector<handle_t> handles;
  handles.reserve(gbz.graph.get_node_count());
  gbz.graph.for_each_handle([&](const handle_t& h) { handles.push_back(h); });
  std::cout << "enumerated\t" << handles.size() << "\n";

  std::mt19937_64 rng(20260920);
  std::shuffle(handles.begin(), handles.end(), rng);
  size_t take = std::min(n_nodes, handles.size());

  // (1) per-node thread multiplicity
  std::vector<size_t> mult;
  mult.reserve(take);
  size_t total_steps = 0;
  auto t2 = clk::now();
  for (size_t i = 0; i < take; i++) {
    size_t c = 0;
    gbz.graph.for_each_step_on_handle(handles[i], [&](const step_handle_t&) { c++; return true; });
    mult.push_back(c);
    total_steps += c;
  }
  auto t3 = clk::now();
  std::sort(mult.begin(), mult.end());
  std::cout << "mult_nodes_sampled\t" << take << "\n";
  std::cout << "mult_total_steps\t"   << total_steps << "\n";
  std::cout << "mult_mean\t"   << (double)total_steps / (double)take << "\n";
  std::cout << "mult_median\t" << pct(mult, 0.50) << "\n";
  std::cout << "mult_p95\t"    << pct(mult, 0.95) << "\n";
  std::cout << "mult_p99\t"    << pct(mult, 0.99) << "\n";
  std::cout << "mult_max\t"    << (mult.empty() ? 0 : mult.back()) << "\n";
  std::cout << "mult_zero_frac\t" << (double)std::count(mult.begin(), mult.end(), (size_t)0) / (double)take << "\n";
  std::cout << "for_each_step_us_per_node\t" << secs(t2, t3) * 1e6 / (double)take << "\n";
  std::cout << "for_each_step_us_per_step\t"
            << (total_steps ? secs(t2, t3) * 1e6 / (double)total_steps : 0.0) << "\n";

  // (2) locate(): resolve a step to its path identity.
  // Collect steps from nodes that actually carry threads.
  std::vector<step_handle_t> steps;
  steps.reserve(n_locate);
  for (size_t i = 0; i < handles.size() && steps.size() < n_locate; i++) {
    gbz.graph.for_each_step_on_handle(handles[i], [&](const step_handle_t& s) {
      steps.push_back(s);
      return steps.size() < n_locate;
    });
  }
  if (!steps.empty()) {
    auto t4 = clk::now();
    uint64_t sink = 0;
    for (const auto& s : steps) sink += as_integer(gbz.graph.get_path_handle_of_step(s));
    auto t5 = clk::now();
    std::cout << "locate_steps\t" << steps.size() << "\n";
    std::cout << "locate_us_per_step\t" << secs(t4, t5) * 1e6 / (double)steps.size() << "\n";
    std::cout << "locate_sink\t" << (sink & 1) << "\n";

    // (3) walk the thread from its start to recover a step offset.
    // This is the operation proposed to replace get_position_of_step for short
    // transcript threads. Measure walk length as well as time.
    size_t walk_n = std::min<size_t>(steps.size(), 300);
    size_t total_walk = 0;
    auto t6 = clk::now();
    for (size_t i = 0; i < walk_n; i++) {
      path_handle_t p = gbz.graph.get_path_handle_of_step(steps[i]);
      size_t offset = 0, hops = 0;
      for (step_handle_t s = gbz.graph.path_begin(p);
           s != gbz.graph.path_end(p);
           s = gbz.graph.get_next_step(s)) {
        hops++;
        if (s == steps[i]) break;
        offset += gbz.graph.get_length(gbz.graph.get_handle_of_step(s));
      }
      total_walk += hops;
      sink += offset;
    }
    auto t7 = clk::now();
    std::cout << "walk_paths\t" << walk_n << "\n";
    std::cout << "walk_mean_hops\t" << (double)total_walk / (double)walk_n << "\n";
    std::cout << "walk_us_per_query\t" << secs(t6, t7) * 1e6 / (double)walk_n << "\n";
    std::cout << "walk_sink\t" << (sink & 1) << "\n";
  }
  return 0;
}
