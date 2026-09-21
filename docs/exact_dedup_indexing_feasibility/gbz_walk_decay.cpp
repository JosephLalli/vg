// How fast does the compatible-transcript set collapse as an alignment extends?
// Starting from a random node, extend a GBWT SearchState one node at a time
// along a thread-supported path and record SearchState.size() at each hop.
// This is the "which transcripts are compatible with this read" query done the
// way a GBWT is meant to be used: compose the constraint, enumerate once at the
// end -- instead of enumerating every thread at every node and intersecting.
// Also reports branching-node discrimination, which uniform node sampling hides.
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
static double secs(clk::time_point a, clk::time_point b){return std::chrono::duration<double>(b-a).count();}
template <typename T> static double pct(std::vector<T>& v,double p){
  if(v.empty())return 0; return (double)v[(size_t)(p*(v.size()-1))];
}

int main(int argc, char** argv) {
  if (argc < 2) { std::cerr << "usage: " << argv[0] << " FILE.gbz [n_walks] [max_hops]\n"; return 2; }
  size_t n_walks  = (argc>2)?std::stoul(argv[2]):20000;
  size_t max_hops = (argc>3)?std::stoul(argv[3]):24;

  GBZ gbz;
  sdsl::simple_sds::load_from(gbz, argv[1]);

  std::vector<handle_t> handles;
  handles.reserve(gbz.graph.get_node_count());
  gbz.graph.for_each_handle([&](const handle_t& h){handles.push_back(h);});
  std::mt19937_64 rng(20260920);
  std::shuffle(handles.begin(), handles.end(), rng);

  // size of the surviving SearchState after h hops
  std::vector<std::vector<size_t>> by_hop(max_hops + 1);
  // discrimination at genuinely branching nodes only
  std::vector<double> minority_frac;
  size_t walks = 0, bp_total = 0;

  auto t0 = clk::now();
  for (size_t i = 0; i < handles.size() && walks < n_walks; i++) {
    gbwt::SearchState st = gbz.index.find(GBWTGraph::handle_to_node(handles[i]));
    if (st.empty()) continue;
    walks++;
    by_hop[0].push_back(st.size());
    handle_t cur = handles[i];
    size_t bp = gbz.graph.get_length(cur);
    for (size_t h = 1; h <= max_hops; h++) {
      // collect thread-carrying successors
      std::vector<std::pair<handle_t,size_t>> succ;
      gbz.graph.follow_edges(cur, false, [&](const handle_t& nx){
        gbwt::SearchState e = gbz.index.extend(st, GBWTGraph::handle_to_node(nx));
        if (!e.empty()) succ.emplace_back(nx, e.size());
        return true;
      });
      if (succ.empty()) break;
      if (succ.size() >= 2) {
        size_t tot = 0, mx = 0;
        for (auto& s : succ) { tot += s.second; mx = std::max(mx, s.second); }
        if (tot) minority_frac.push_back(1.0 - (double)mx/(double)tot);
      }
      // follow the most-supported branch (the modal alignment)
      auto best = std::max_element(succ.begin(), succ.end(),
                   [](auto&a, auto&b){return a.second < b.second;});
      st = gbz.index.extend(st, GBWTGraph::handle_to_node(best->first));
      cur = best->first;
      bp += gbz.graph.get_length(cur);
      by_hop[h].push_back(st.size());
      if (st.size() <= 1) break;
    }
    bp_total += bp;
  }
  auto t1 = clk::now();

  std::cout << "walks\t" << walks << "\n";
  std::cout << "mean_bp_per_walk\t" << (double)bp_total/(double)walks << "\n";
  std::cout << "us_per_walk\t" << secs(t0,t1)*1e6/(double)walks << "\n";
  std::cout << "hop\tn\tmedian\tp95\tmean\n";
  for (size_t h = 0; h <= max_hops; h++) {
    auto& v = by_hop[h];
    if (v.empty()) continue;
    std::sort(v.begin(), v.end());
    long double s=0; for(size_t x:v) s+=x;
    std::cout << h << "\t" << v.size() << "\t" << pct(v,0.50) << "\t"
              << pct(v,0.95) << "\t" << (double)(s/v.size()) << "\n";
  }
  std::sort(minority_frac.begin(), minority_frac.end());
  std::cout << "branching_events\t" << minority_frac.size() << "\n";
  if (!minority_frac.empty()) {
    long double s=0; for(double x:minority_frac) s+=x;
    std::cout << "minority_branch_frac_mean\t" << (double)(s/minority_frac.size()) << "\n";
    std::cout << "minority_branch_frac_median\t" << pct(minority_frac,0.50) << "\n";
    std::cout << "minority_branch_frac_p95\t" << pct(minority_frac,0.95) << "\n";
  }
  return 0;
}
