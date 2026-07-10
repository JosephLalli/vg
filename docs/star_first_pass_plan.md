# STAR First-Pass on Graphs: Implementation Plan (items 2, 3, 6, 7, 5)

> **Doc status (2026-07-10):** Items 2/3/6/7/5 IMPLEMENTED on branch `mmp-splice-seeding`, default-off
> byte-identical. `--sj-out` (item 7) is the shipped first-pass deliverable used by the chr20 benchmark.
> Item 6 (`--sjdb-score`) is partial; the proposed `--mmp-extend` was implemented then **removed**
> (it degraded mapping — see item 3). Benchmarks below are on the **MHC pangenome fixtures**.
> Current state + chr20 benchmark: **`mpmap_vs_star_sj_STATUS.md`**.

## Goal and scope

Faithfully re-implement the **first pass** of STAR's two-pass method on the vg graph mapper.
The first pass = normal mapping + on-the-fly novel splice-junction discovery, whose output is a
junction set (STAR's `SJ.out.tab`). It does NOT insert junctions or remap -- that is pass 2
(enumeration item 1), deferred. Multimapping / MAPQ / output filters (item 9) are OUT of scope:
much of that is handled by the accompanying panCollapse.

The MMP seeding core is already implemented (see `mmp_star_parity_plans.md`): sequential MMP,
chaining, both soft-clip tails, whole-read primary/augment seeding, motif+intron splice scoring
(`SpliceStats`), the `gcsa->order()` extension cap. This plan closes the remaining first-pass
gaps.

Implementation order (item 5 deferred to last, per instruction):
**2 (multi-start seeding) -> 3 (mismatch handling) -> 6 (annotated-junction scoring) ->
7 (junction collection + SJ output) -> 5 (both-strand primary seeding).**

## Implementation status (2026-07-08)

All five items implemented, flag-gated, default-off byte-identical; tests 35 (31/31), 33 (25/25).
Benchmarks on the PANGENOME (sampleA.spliced, 466-hap).

- **Item 2 (multi-start seeding) -- DONE, tuned to mirror MEM.** `--mmp-start-lmax`,
  `--mmp-start-lmax-over-lread`, `--mmp-seed-per-read-max`. Multi-start anchors MMP chains at a
  ladder of read offsets so seeds overlap and densify. DEFAULTS tuned so the STAR mode mirrors
  MEM finding's graph-tuned seed density (MEM's defaults are graph-tuned, so we target its
  stats): `--mmp-start-lmax 6`, `--mmp-min-prefix 8`. On the pangenome 20k this matches MEM
  almost exactly -- cluster_graphs/read 75.4 vs 74.5, cg_nodes/read 822 vs 831, mapped 3,458 vs
  3,461 (full sensitivity), vs the sparse single-start baseline (lmax 50: 3,305 mapped, 19.5
  cluster_graphs). Raising `--mmp-start-lmax` trades sensitivity for speed (lmax 12: 3,407 mapped
  at ~2.5x MEM's speed). NOTE: `--mmp-hit-max` is NOT the density lever -- raising it 16 -> 1024
  recovered only +4 reads; seed COUNT (multi-start + min-prefix), matching MEM's reseeding
  density, is what matters. Trace `n_mmp_seed_starts`.
- **Item 3 (mismatch handling) -- DONE (re-seed only; seed-level extension removed).**
  Seeds stay EXACT; mismatches are handled by a clean re-seed across the break (STAR's re-seed).
  Mismatch/gap-tolerant EXTENSION is delegated to `multipath_align`, which already extends seeds
  into scored alignments allowing mismatches and gaps at the seed's true locus -- this IS STAR's
  extendAlign, done natively by the aligner, so a seed-level extension is redundant. A prior
  `--mmp-extend` that "substituted the read base and re-queried the index" was implemented and
  benchmarked: it did not explode (the greedy single-path guard worked) but it DEGRADED mapping
  (3,407 -> 2,367) because it searched for a sequence the read does not contain, fabricating false
  anchors on paralogs (seeds/read rose 11.7 -> 16.1 while mapped fell). It was the wrong operation
  -- STAR extends a LOCATED alignment keeping the read and scoring mismatches, never re-querying
  the index -- so it was REMOVED. Trace `n_mmp_reseeds`.
- **Item 6 (annotated-junction sjdb) -- DONE (partial, documented).** `--sjdb-score` adds a bonus
  to a spliced alignment's score when the junction reuses an existing graph edge (annotated); the
  annotated flag is computed by an edge-adjacency check. Caveat: the rescue path (where the hook
  lives) discovers NOVEL junctions, so annotated cases are rare there; full annotated coverage
  needs the post-alignment scan noted under item 7.
- **Item 7 (SJ output) -- DONE.** `--sj-out FILE` emits a graph-native junction table (donor/
  acceptor node:offset:strand, motif, annotated, unique/multi read support, max overhang). 20k
  pangenome: 397 junctions with support counts. Motif reported via `unoriented_motif` (canonical
  donor+acceptor label). SCOPE: captures rescue-DISCOVERED (novel) junctions; junctions traversed
  via existing graph splice edges during normal alignment are not yet captured -- a post-alignment
  path scan is the reserved addition (and would populate the annotated rows for item 6).
- **Item 5 (both-strand) -- DONE (investigation: already covered).** vg's GCSA2 is double-stranded,
  so a forward-read query already returns both-strand hits. Confirmed: reverse-strand reads map
  under `--mmp-primary`. No RC-seeding code needed; a `--mmp-both-strands` toggle is unnecessary.

Grounding (current code):
- Seeding: `src/mpmap_mmp.cpp` `generate_primary_seeds` / `run_mmp` / `emit_chain`.
- Splice rescue: `src/multipath_mapper.cpp` `find_spliced_alignments` (:4042 single / :4303 paired),
  `test_splice_candidates` (:2781; distance+motif enumeration `MotifPairIterable` :2906;
  net score `post_align_net_score` :2896; splice-edge realized ~:3386).
- Splice scoring: `src/splicing.hpp` `SpliceStats` (motif_score :57, intron_length_score :61).
- Thread-safe sink pattern to mirror: `src/mpmap_trace.{hpp,cpp}` (mutex-guarded file + config).

All new behavior is flag-gated and default-off (default mapping output byte-identical), following
the existing `mpmap_mmp` pattern.

---

## Item 2 -- Multi-start-point seeding (`seedSearchStartLmax`)

**Current.** `generate_primary_seeds` runs ONE sequential chain anchored at the read end
(`qe = seq.end()`), covering the read once (~5 seeds/read on the pangenome). This is the sensitivity
gap: MEM finding produces ~40 seeds/read; pure MMP loses ~6% of mappable reads.

**STAR.** Splits the read into pieces no longer than `seedSearchStartLmax` (default 50, or
`seedSearchStartLmaxOverLread` x readLen, whichever smaller) and launches an MMP search from each
start point, yielding overlapping seed sets.

**Graph design.** GCSA2 backward search finds the maximal exact SUFFIX ending at an offset, so a
"start point at offset s" = a chain whose first seed ends at s. Run `emit_chain`-style chains
anchored at a ladder of offsets:
  `s in { L, L - step, L - 2*step, ... , >= min_prefix }`, step = min(start_lmax, L)
where L = read length. Each anchor runs the existing sequential chain (so within an anchor we still
get MMP1->MMP2->... across mismatch/junction breaks). Dedup identical seeds (same read interval +
matched range) so overlapping anchors don't double-count; clustering tolerates the rest.

**Parameters (hidden):** `--mmp-start-lmax` (default 50), `--mmp-start-lmax-over-lread` (default
1.0). Reuse `--mmp-max-seeds` / a new `--mmp-seed-per-read-max` (STAR `seedPerReadNmax` analog,
default 1000) as the global cap.

**Files.** `mpmap_mmp.cpp` (loop the chain over the offset ladder in `generate_primary_seeds`, and
optionally in the splice-rescue tails), `mpmap_main.cpp` (flags), `mpmap_trace` (already counts
`n_mmp_primary_seeds`; add `n_mmp_seed_starts`).

**Risk.** Low -- still pure exact matching, no branching. Cost is linear in the number of start
points (bounded by the caps). This is the SAFE density fix.

**Verify.** On the PANGENOME (`sampleA.spliced`, 200k reads): `--mmp-primary` mapped count should
climb from 31,844 toward the 34,006 MEM baseline as start points increase; seeds/read should rise
from ~5 toward the STAR-like range. Report mapped / seeds-per-read / runtime vs start-lmax.

**Effort.** Moderate.

---

## Item 3 -- Mismatch-tolerant seed extension (`extendAlign`)  [HANDLE WITH CARE]

**Current.** The MMP walk stops at the first mismatch (pure exact match); a blocking base is
crudely skipped one at a time.

**STAR.** When an MMP ends at a mismatch (not a splice), STAR (a) re-seeds after the mismatch AND
(b) extends the alignment through mismatches (bounded by the local score) to lengthen the seed /
reach read ends.

**WHY THIS IS THE EXPLOSION RISK ON A GRAPH (the crux).**
- Exact GCSA walk is linear: each step either shrinks the BWT range (match) or empties it
  (mismatch -> stop).
- "Extend through a mismatch" on a GCSA range means the read base does not match, so to continue
  you must try the OTHER bases (branch to A/C/G/T) and carry each as a separate range. With k
  allowed mismatches that is up to 3^k branches, AND on a dense pangenome each branch stays
  non-empty (real alt alleles), so the number of live ranges multiplies at every mismatch.
  Allowing mismatches at arbitrary positions is exponential -> can add HOURS.
- STAR avoids this because on a LINEAR genome extension happens at ONE located locus: a mismatch is
  just "count it and step to the next base of that locus" -- O(1) branching. During GCSA seeding
  there is no single locus (a range = many loci), which is exactly what makes graph mismatch
  extension dangerous.
- Extra graph subtlety: on a well-built pangenome, most reference-mismatches are KNOWN variants =
  graph BRANCHES that GCSA already matches EXACTLY on an alt path. So graph seeding already absorbs
  known variation for free; mismatch extension is only needed for sequencing ERRORS and NOVEL
  variants -- a much smaller need than linear STAR, which further argues for a conservative,
  optional design.

**Safe design (staged, default-off, benchmarked -- REQUIRED).**
1. **Default: re-seed across the mismatch (exact, no branching).** Replace the one-base skip with a
   clean STAR-style re-seed: on an empty range, record the terminated seed, then start a fresh exact
   MMP one base past the mismatch, keeping BOTH flanking seeds. Zero branching, zero explosion risk.
   This captures STAR's "seed across a mismatch" behavior safely and is the default.
2. **Optional: locate-then-graph-extend (flag `--mmp-extend`, OFF by default).** Only AFTER a seed
   is LOCATED (concrete graph positions), extend each located position through the graph with a
   BOUNDED number of mismatches using vg's existing vetted extender (GaplessExtender, as used by
   giraffe) or the aligner's banded tail extension -- NOT a GCSA range walk. This bounds branching
   to the real graph neighborhoods of already-located seeds and reuses tested code.
   - Hard bounds: `--mmp-extend-max-mismatch` (default 1), `--mmp-extend-max-length` (cap bases
     extended), and a per-read work cap that aborts extension if exceeded.
3. **FORBIDDEN: mismatch-tolerant GCSA range walking** (branching the BWT range on a mismatch).
   Do not implement; it is the exponential trap.

**Benchmark gate (mandatory before enabling by default -- which we will NOT do without this).**
After implementing (2), benchmark `--mmp-extend` on the full pangenome at 200k+ reads and report
wall time vs off; measure the extension-work distribution (a long tail of reads consuming huge
extension budgets is the failure signature). Keep it flag-gated until benchmarked clean.

**Files.** `mpmap_mmp.cpp` (re-seed logic in the walk; a separate `mmp_extend` helper calling
`GaplessExtender` / aligner), `mpmap_main.cpp` (flags), `mpmap_trace` (`n_mmp_reseeds`,
`time_mmp_extend_usec`, `n_mmp_extend_aborted`).

**Risk.** (1) low; (2) HIGH runtime risk if unbounded -> the bounds + benchmark + default-off are
non-negotiable.

**Effort.** (1) low; (2) moderate-high (careful).

---

## Item 6 -- Annotated-junction scoring (STAR `sjdb`)

**Current.** `SpliceStats` scores splices by motif + intron length (`test_splice_candidates` /
`post_align_net_score`). There is no distinction between a junction that is ALREADY an edge in the
graph (annotated, e.g. from `vg rna`) and a NOVEL junction discovered by rescue.

**STAR.** Known junctions in the `sjdb` get a score bonus (`sjdbScore`, default 2) and a RELAXED
overhang requirement (`alignSJDBoverhangMin` < novel `alignSJoverhangMin`), so annotated junctions
are preferred and accepted with shorter flanks.

**Graph design.** The graph's existing splice edges ARE the sjdb. Classify each candidate splice as
annotated (its donor->acceptor is realized by an existing graph edge / adjacency) vs novel
(introduced by rescue). Then in `test_splice_candidates`:
  - add `sjdb_score` to the net splice score for annotated junctions;
  - apply `sjdb_overhang_min` (relaxed) for annotated vs `novel_overhang_min` for novel, replacing
    the single `min_softclip_length_for_splice` gate for the annotated case.
Detecting "annotated": check whether the candidate donor/acceptor graph positions are connected by
an existing edge in `xindex` (the spliced graph carries these), or whether the splice reuses a
graph edge rather than a rescue-inserted one.

**Parameters (hidden):** `--sjdb-score` (default 2), `--sjdb-overhang-min` (default 3),
`--novel-overhang-min` (default = current `min_softclip_length_for_splice`).

**Files.** `multipath_mapper.cpp` `test_splice_candidates` (the score + overhang gate),
`mpmap_main.cpp` (flags). Possibly a small helper to test edge existence in `xindex`.

**Risk.** Moderate -- touches splice acceptance scoring; must preserve default behavior when the
knobs are at neutral values (sjdb-score 0 -> identical). Verify default-off byte-identical.

**Verify.** On the pangenome, annotated (graph-edge) junctions should be preferred over novel when
both are plausible; regression that canonical splices are unchanged at sjdb-score 0.

**Effort.** Moderate.

---

## Item 7 -- Novel-junction collection + SJ output (the FIRST-PASS DELIVERABLE)

**Current.** Splices are discovered per-read in `find_spliced_alignments` but never aggregated or
reported; the trace records per-read splice activity but there is no junction-level output.

**STAR.** `SJ.out.tab`: one row per discovered junction with intron start/end, strand, motif class,
annotated flag, # uniquely-mapping reads, # multi-mapping reads, and max spliced overhang. This IS
the first-pass output that pass 2 consumes.

**Graph design.** Add a thread-safe **junction collector** (mirror the `mpmap_trace` sink: a
`mutex`-guarded map + config, enabled by a flag). When a splice is ACCEPTED in
`find_spliced_alignments` (`did_splice`), record its junction key and increment support:
  - key: (donor graph position, acceptor graph position, strand) -- graph-native, unambiguous.
  - per-junction: unique-read count, multi-read count (from the read's multiplicity), motif class,
    annotated flag (from item 6), max overhang.
At `close()`, emit the table. Coordinate reporting has two modes:
  - **graph-native** (default): donor/acceptor as `node_id:offset:orientation` -- exact, no
    projection needed, and directly usable to insert edges in pass 2.
  - **reference-projected** (`--sj-ref-path NAME`): project donor/acceptor onto a named reference
    path via `xindex` path-position queries (like the truth-join did for the linear graph; for the
    pangenome use `get_position_of_step` / nearest reference path), to emit STAR-style chrom:pos.

**Parameters:** `--sj-out FILE` (enable + path), `--sj-ref-path NAME` (optional projection),
`--sj-min-overhang` / `--sj-min-reads` (STAR `outSJfilter*` analogs; default permissive for the
first pass).

**Files.** New `src/mpmap_sj.{hpp,cpp}` (collector + writer, sink pattern like `mpmap_trace`);
one recording call in `find_spliced_alignments` at splice acceptance; `mpmap_main.cpp` (flags +
open/close). `multipath_mapper.hpp` untouched (free-function sink, like the trace layer).

**Risk.** Moderate -- thread safety (mutex on accept, or per-thread accumulate + merge at close to
avoid hot-path contention); coordinate projection on the pangenome is the fiddly part (graph-native
mode avoids it).

**Verify.** On the pangenome, the emitted junctions should include the graph's annotated junctions
(high support) plus novel ones; cross-check support counts against `splice_changed_primary` totals;
on the de-novo control, novel junctions should match the truth GTF at the rate the truth-join
already measured.

**Effort.** Moderate-high (this is the headline deliverable).

---

## Item 5 -- Both-strand primary seeding  [LAST]

**Current.** `generate_primary_seeds` walks the forward read only.

**STAR.** Seeds the read and its reverse complement.

**Graph nuance (investigate first).** A FORWARD-read GCSA query already returns hits on BOTH graph
strands (the `rc` bit in `make_pos_t`), so antisense placement *within the graph* is covered. What
is NOT covered is the read itself being sequenced antisense to the transcript. Determine whether vg
mpmap already maps both read orientations upstream (if so, MMP primary seeding must MATCH that by
seeding both orientations; if not, this item is how antisense reads get seeded). This dependency on
existing strand handling is why item 5 is sequenced last.

**Design.** If needed: also run the whole-read walk on `reverse_complement(seq)`, emitting seeds
with read intervals mapped back to the forward read and positions strand-flipped (reuse the RC
relocation from `run_mmp`). Dedup against forward-orientation seeds.

**Parameters:** fold into `--mmp-primary` / `--mmp-augment` (both strands by default once
validated), or a `--mmp-both-strands` toggle if a comparison is wanted.

**Files.** `mpmap_mmp.cpp`, `mpmap_main.cpp`.

**Risk.** Low-moderate; mainly the investigation of existing strand handling to avoid double-work.

**Verify.** Antisense reads (or a strand-balanced set) map at parity with forward reads; no
regression on default.

**Effort.** Low-moderate.

---

## What this enables (and what is still deferred)

After items 2/3/6/7/5, the FIRST PASS is faithful: STAR-like multi-start seeding, safe (and
optionally extended) mismatch handling, annotated-vs-novel junction scoring, and an emitted
junction set (SJ output). The remaining STAR piece, **pass 2 (enumeration item 1)** -- insert the
discovered junctions as graph edges, re-index, and remap -- is the natural next project and is
exactly what item 7's graph-native junction keys are designed to feed. Multimapping / MAPQ / output
filtering remain delegated to panCollapse.
