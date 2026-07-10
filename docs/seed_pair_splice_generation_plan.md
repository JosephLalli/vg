# Plan: seed-pair-driven splice candidate generation for mpmap

> **Doc status (2026-07-10):** Approach **REFUTED** — implemented behind `--mmp-splice-pairs`
> (default off) and did NOT recover non-canonical precision (see VALIDATION RESULT at the end).
> Retained as the record of *why* seed-pair candidate gating is not the precision lever. Substrate:
> chr20. Current state + the active next step: **`mpmap_vs_star_sj_STATUS.md`** and
> `star_vs_mpmap_sj_precision_diagnostic_plan.md`.

## Problem this solves
Benchmarking (chr20 positive control, STAR as reference) showed:
- With the relaxed motif-frequency budget + a curated non-canonical motif set, mpmap now
  matches/exceeds STAR on **non-canonical recall** (17/30 vs STAR 10/30), canonical preserved.
- But **non-canonical precision is ~3%** vs STAR's ~91%, and it is NOT filterable: false
  non-canonical junctions have *higher* overhang (median 56) than true ones (median 31), deep
  read support, and are scattered — i.e. structurally indistinguishable from real junctions by
  any read-level metric.

Root cause is architectural. Today's splice rescue enumerates *every* admitted-motif
dinucleotide inside a window and pairs anchor motif positions with candidate motif positions
(`test_splice_candidates`, `multipath_mapper.cpp:2831`; `SpliceRegion` built at :3169/:3217 with
a `2 * max_splice_overhang` window). With non-canonical motifs admitted, dinucleotides are dense,
so for a read the mapper finds many equally-scoring balanced splices and reports spurious ones.

STAR is immune because its splice candidates come only from **split-seed pairs**: a junction is
proposed only where the read's own two Maximal-Mappable-Prefix seeds map to distinct, intron-range
loci, with the exact breakpoint refined to the best motif inside the small seed-overlap window.
The read's evidence — not the genome's motif density — defines the candidate set.

## Core idea
Replace/augment the motif-scan candidate generator with one whose candidate breakpoints are
**exactly the read split points implied by MMP seed pairs**. Per seed pair, propose at most one
breakpoint (best motif inside the tiny overlap window), not O(window × motifs). A spurious
non-canonical partner then requires an actual read seed to map there, which is rare on unique
sequence — so precision should approach STAR's while the MMP re-seeding preserves the recall win.

## What already exists (reuse, don't rebuild)
`src/mpmap_mmp.{hpp,cpp}` — the STAR-style MMP layer (branch `mmp-splice-seeding`):
- `generate_mmp_seeds(...)` (hpp:~124): breakpoint-pinned MMP re-seed of one soft-clipped tail —
  pins a positioned partner-exon seed even for short overhangs. This is the split-seed we need.
- `generate_primary_seeds(...)`: whole-read sequential MMP walk (seeds naturally split at
  junctions). `MmpParams` already carries `overlap_tol`, `min_intron`, `max_intron`, `hit_max`,
  `start_lmax`, `strand_mode`.
- `is_mmp_seed(...)`, `reset_read_store()` — thread-local pointer-stable seed store.

Today these seeds only *feed the existing candidate pool* (augment/primary), then the same
motif-scan runs downstream. The change is to let the seed pair *define the breakpoint directly*.

## Design

### New data carried out of the MMP layer
Extend `generate_mmp_seeds` (or add `generate_splice_pairs`) to emit, per partner seed, a
`SeededBreakpoint`:
```
struct SeededBreakpoint {
    int64_t anchor_read_end;     // read offset where the anchor's aligned block ends (donor side)
    int64_t partner_read_start;  // read offset where the partner seed begins (acceptor side)
    pos_t   anchor_graph_pos;    // graph position at anchor_read_end (donor)
    pos_t   partner_graph_pos;   // graph position at partner_read_start (acceptor)
    bool    search_left;         // which tail
    // overlap = anchor_read_end - partner_read_start may be small +/- (seed overlap slack)
};
```
`anchor_graph_pos` already comes from `trimmed_end(opt, max_splice_overhang, ...)`
(`multipath_mapper.cpp:3166`); `partner_graph_pos` is the located MMP seed hit.

### Constrained candidate generation (the actual change)
In `test_splice_candidates` (`multipath_mapper.cpp:2831`), gate a new path on
`mpmap_mmp::splice_pairs_enabled()`:

1. Do NOT build the `2 * max_splice_overhang` motif-scan `SpliceRegion` per candidate.
2. Instead, for each `SeededBreakpoint`, restrict the search to a window of `±overlap_tol` bp on
   each side:
   - donor window: graph interval around `anchor_graph_pos` within `overlap_tol`
   - acceptor window: graph interval around `partner_graph_pos` within `overlap_tol`
   Add a `SpliceRegion` constructor variant (or a `max_search_dist` arg) in
   `src/splicing.{hpp,cpp}` that bounds enumeration to this tiny window instead of
   `2 * max_splice_overhang`.
3. Within the two tiny windows, enumerate only the (few) motif positions and choose the single
   breakpoint with the best combined `splice_stats.motif_score(donor) + motif_score(acceptor)`
   (canonical wins ties; non-canonical admitted with its penalty). Emit ONE `PutativeJoin` per
   seed pair (or top-1), not the cross-product.
4. Keep the existing spliceable-distance prune (`min_intron..max_intron`) and acceptance test
   (`net_score > no_splice_log_odds`, :3463) unchanged. The candidate SET is now tiny and
   read-backed; scoring/acceptance is identical.

### Why precision recovers
A candidate breakpoint now exists only where an actual read seed maps to a partner locus in
intron range. Random genomic non-canonical dinucleotides are no longer candidates. The remaining
false positives are limited to genuine paralog/repeat seed hits (further reducible by `hit_max`
and by requiring the partner seed length ≥ a threshold), rather than motif-density artifacts.

## Gating / config
- New `MmpParams` fields: `bool splice_pairs = false;` and `int64_t splice_pair_window = overlap_tol;`
- New flag `--mmp-splice-pairs` in `mpmap_main.cpp` (mirrors the existing `--mmp-seed` block).
- Default OFF → byte-identical to current behavior (verify with existing golden tests).
- Modes: (a) *replace* — seed-pair candidates are the only splice candidates (cleanest precision);
  (b) *augment* — union with the current motif-scan (safer recall, weaker precision). Start with
  replace behind the flag.

## Files touched
- `src/mpmap_mmp.hpp/.cpp` — `SeededBreakpoint`, `generate_splice_pairs()`, new params, accessor.
- `src/splicing.hpp/.cpp` — `SpliceRegion` bounded-window variant (or `max_search_dist` param).
- `src/multipath_mapper.cpp` — gated branch in `test_splice_candidates` (~:3154-3234) that builds
  `PutativeJoin`s from `SeededBreakpoint`s instead of the motif-scan `SpliceRegion`s.
- `src/subcommand/mpmap_main.cpp` — `--mmp-splice-pairs` flag + `configure()` wiring.
- `src/mpmap_sj.cpp` — none (the populated `max_overhang` already lands); optionally emit
  partner-seed length as an extra `--sj-out` column for downstream filtering.

## Validation
1. `test/t/33_vg_mpmap.t` + `test/t/35_vg_mpmap_trace.t` stay green; default-off byte-identical
   (diff a GAMP dump with/without the new TU).
2. Positive-control benchmark (`$CLAUDE_JOB_DIR/tmp/chr20bench`): re-run MEM and MMP with
   `--mmp-splice-pairs` on `reads_pc.fq`; score with `score_pc.py`. Targets:
   - non-canonical **precision** ≫ 3% (goal: approach STAR ~90%),
   - non-canonical **recall** ≥ current (17/30) and canonical ≥ 12/15,
   - total discovered junctions drop from ~900 toward STAR's ~140.
3. Candidate-count instrumentation: log candidates/read; expect O(#read splits) not O(window×motifs).
4. Accuracy regression: gamcompare on the 20k pangenome reads unchanged (~93%).

## Risks / open questions
- **Breakpoint ambiguity** within `overlap_tol`: the in-window motif pick handles it (prefer
  canonical; else the specific non-canonical). Widen the window only if recall drops.
- **Reverse-strand tails**: reuse `strand_mode` (already native/rc/both in the MMP layer).
- **Paralog seed hits** still admit some false pairs on the pangenome — orthogonal precision lever
  (partner-seed uniqueness / `hit_max`), not solved here but much smaller than motif-density noise.
- **Cost**: one MMP re-seed per soft-clip tail; bounded by `hit_max`, `start_lmax`. Measure wall
  time on the 20k set (prior MMP modes were +3–10%).
- **Interaction with `--mmp-primary/-augment`**: those change the *seed pool*; this changes
  *splice candidate generation*. They compose but should be tested pairwise.

## Effort
~2–4 focused days: (1) `SeededBreakpoint` + `generate_splice_pairs` (reuses `generate_mmp_seeds`
internals), (2) bounded `SpliceRegion`, (3) gated branch in `test_splice_candidates`, (4) flag +
tests + benchmark. The scoring/acceptance and distance machinery are reused unchanged.

## VALIDATION RESULT (2026-07-10) — plan hypothesis REFUTED
Implemented on branch `mmp-splice-seeding` (all default-off byte-identical, verified):
- `--mmp-splice-pairs` flag + `MmpParams::splice_pairs` (mpmap_mmp.{hpp,cpp}, mpmap_main.cpp).
- Partner constraint: skip cluster + raw-MEM splice partners, keep only breakpoint-pinned MMP
  seeds (multipath_mapper.cpp identify_unaligned_splice_candidates).
- STAR-style mismatch constraint (from consulting STAR `stitchAlignToTranscript.cpp`,
  `alignSJstitchMismatchNmax=0` for non-canonical): reject a non-canonical join whose connecting
  (stitched) alignment carries any substitution (multipath_mapper.cpp splice-acceptance loop).

Positive-control (3600 reads, curated motifs). NOTE: measured on a `vg construct` reference graph
(sequence-equivalent diagnostic); the authoritative substrate is `refpath` = HPRC chr20 pangenome
pruned to the `CHM13#0#chr20` reference path — see `mpmap_vs_star_sj_STATUS.md`. Re-measurement on
`refpath` in progress:
- baseline (no flag):        canon 12/15, non-canon recall 17/30, non-canon precision 3% (29/902)
- --mmp-splice-pairs:         canon 13/15, recall 17/30, precision 3% (1152 total — MMP seeds add partners)
- + mismatch constraint:      canon 13/15, recall 17/30, precision 3% (632 non-canon reported, 22 true)
- STAR pass-1:                canon 12/15, recall 10/30, precision ~91% (11 non-canon reported, 10 true)

ROOT CAUSE (revised): the spurious non-canonical splices are NOT candidate-source or stitch-quality
artifacts. They are CLEAN alignments (0 stitch mismatches, overhang up to 56, multi-read support) of
reads to PARALOG partner loci. Neither partner-source restriction, stitch-mismatch cap, overhang, nor
read-count filters separate them from true junctions.

The real difference from STAR is the alignment SELECTION architecture: STAR aligns the WHOLE read to
its single best genomic window (both exons must match well, so a paralog partner loses to the true
locus). mpmap's splice rescue anchors ONE exon and rescues the soft-clipped tail to any scoring
partner, so it accepts a spurious paralog splice that is locally optimal for the tail. Closing this
needs whole-read best-window alignment selection (a splice-rescue re-architecture), or partner
uniqueness gating (reject tail seeds that map to multiple loci), or Portcullis/DeepSplice-style ML
post-filtering (the field standard even for STAR-based pipelines). The motif-model + relaxed-budget
work already achieves STAR-LEVEL RECALL; precision is the remaining, architecturally deeper gap.
