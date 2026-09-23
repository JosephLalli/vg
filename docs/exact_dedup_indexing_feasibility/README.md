# Evidence for the exact-dedup indexing feasibility assessment

Measured 2026-09-20 on chr21's retained exact arm, pinned binary
`4f495d705c5547a39d1334a9c6cd7d4e02ece9e50fe65831ea79ba2679b4273c`.
The assessment itself is `../exact_dedup_indexing_feasibility.md`;
`RECEIPTS.md` here holds every measured number with its command.

The four probes link against the fork's own static libraries. Build each with:

    g++-15 -std=c++20 -O2 -fopenmp -I include <probe>.cpp -o <probe> \
      lib/libgbwtgraph.a lib/libgbwt.a lib/libhandlegraph.a lib/libsdsl.a \
      lib/libdivsufsort.a lib/libdivsufsort64.a \
      -L$HOMEBREW/opt/zstd/lib -L$HOMEBREW/opt/openssl@3/lib -L$HOMEBREW/lib \
      -lzstd -lcrypto -lpthread

(`gbwt_rindex_locate.cpp` needs neither `libgbwtgraph.a` nor `-lcrypto`.)

| document | what it holds |
|---|---|
| `RECEIPTS.md` | every measured figure with its command and binary anchor |
| `GBZ_INDEXING_SURVEY.md` | the adversarially verified survey of what GBZ mapping can improve |
| `MPMAP_INDEX_PIPELINE_PROPOSAL.md` | a pipeline proposal and the NO-GO critique that killed it |
| `WAY_FORWARD.md` | what to do next, what is blocked on which check, and what not to do |

| probe | what it measures |
|---|---|
| `gbz_multiplicity.cpp` | per-node thread multiplicity; `for_each_step_on_handle` and `locate()` cost; thread-walk cost for a step offset |
| `gbz_discrimination.cpp` | node versus single-edge multiplicity, branch counts, best-branch share |
| `gbz_walk_decay.cpp` | how the candidate set shrinks as a SearchState extends along a read |
| `gbwt_rindex_locate.cpp` | plain GBWT `locate()` per position against `FastLocate::locate(SearchState)` in bulk |

These are measurement probes, not fork code. They are kept here because the
numbers in the assessment are not reproducible without them.
