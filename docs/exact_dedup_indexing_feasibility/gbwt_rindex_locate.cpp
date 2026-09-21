// Does the r-index fix the enumeration cost?
// Compares, on the same nodes:
//   (a) plain GBWT: find(node) then index.locate() per position
//   (b) r-index   : find(node) then FastLocate::locate(state) in bulk
// (b) is the correct API for "which transcripts visit this node".
#include <gbwt/gbwt.h>
#include <gbwt/fast_locate.h>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>
#include <algorithm>
#include <string>
#include <fstream>

using clk = std::chrono::steady_clock;
static double secs(clk::time_point a, clk::time_point b){return std::chrono::duration<double>(b-a).count();}
template <typename T> static double pct(std::vector<T>& v,double p){
  if(v.empty())return 0; return (double)v[(size_t)(p*(v.size()-1))];
}

int main(int argc, char** argv) {
  if (argc < 3) { std::cerr << "usage: " << argv[0] << " guide.gbwt chr21.ri [n_nodes]\n"; return 2; }
  size_t n_nodes = (argc>3)?std::stoul(argv[3]):2000;

  gbwt::GBWT index;
  auto t0 = clk::now();
  sdsl::simple_sds::load_from(index, argv[1]);
  auto t1 = clk::now();
  gbwt::FastLocate r_index;
  {
    std::ifstream rin(argv[2], std::ios_base::binary);
    if (!rin) { std::cerr << "cannot open " << argv[2] << "\n"; return 2; }
    r_index.load(rin);
  }
  r_index.setGBWT(index);
  auto t2 = clk::now();
  std::cout << "gbwt_load_s\t"    << secs(t0,t1) << "\n";
  std::cout << "rindex_load_s\t"  << secs(t1,t2) << "\n";
  std::cout << "sequences\t"      << index.sequences() << "\n";

  // Sample GBWT nodes that carry threads.
  std::vector<gbwt::node_type> nodes;
  for (gbwt::node_type n = index.firstNode(); n < index.sigma(); n++) {
    if (index.contains(n)) nodes.push_back(n);
  }
  std::mt19937_64 rng(20260920);
  std::shuffle(nodes.begin(), nodes.end(), rng);
  size_t take = std::min(n_nodes, nodes.size());
  std::cout << "gbwt_nodes\t" << nodes.size() << "\n";

  // (b) r-index bulk locate
  std::vector<size_t> found;
  size_t total = 0; uint64_t sink = 0;
  auto t3 = clk::now();
  for (size_t i = 0; i < take; i++) {
    gbwt::SearchState st = index.find(nodes[i]);
    if (st.empty()) continue;
    std::vector<gbwt::size_type> res = r_index.locate(st);
    found.push_back(res.size());
    total += res.size();
    if (!res.empty()) sink += res[0];
  }
  auto t4 = clk::now();
  std::sort(found.begin(), found.end());
  std::cout << "rindex_nodes_done\t"    << found.size() << "\n";
  std::cout << "rindex_threads_total\t" << total << "\n";
  std::cout << "rindex_mean_threads\t"  << (double)total/(double)found.size() << "\n";
  std::cout << "rindex_median_threads\t"<< pct(found,0.50) << "\n";
  std::cout << "rindex_us_per_node\t"   << secs(t3,t4)*1e6/(double)found.size() << "\n";
  std::cout << "rindex_us_per_thread\t" << (total? secs(t3,t4)*1e6/(double)total : 0.0) << "\n";

  // (a) plain GBWT locate, per position, on a smaller sample
  size_t small = std::min<size_t>(take, 60);
  size_t total2 = 0;
  auto t5 = clk::now();
  for (size_t i = 0; i < small; i++) {
    gbwt::SearchState st = index.find(nodes[i]);
    for (gbwt::size_type p = st.range.first; p <= st.range.second; p++) {
      sink += index.locate(st.node, p); total2++;
    }
  }
  auto t6 = clk::now();
  std::cout << "plain_nodes\t"        << small << "\n";
  std::cout << "plain_positions\t"    << total2 << "\n";
  std::cout << "plain_us_per_node\t"  << secs(t5,t6)*1e6/(double)small << "\n";
  std::cout << "plain_us_per_thread\t"<< (total2? secs(t5,t6)*1e6/(double)total2 : 0.0) << "\n";
  std::cout << "sink\t" << (sink & 1) << "\n";
  return 0;
}
