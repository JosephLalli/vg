# Plan: determine WHY STAR out-precisions vg mpmap on splice-junction calling

> **Doc status (2026-07-10):** ACTIVE plan — the current next step for the open non-canonical
> precision gap. Not yet executed. Substrate: chr20 `refpath`. Current state:
> **`mpmap_vs_star_sj_STATUS.md`**.

## Objective
On the identical chr20 positive-control reads, STAR pass-1 reports ~91% non-canonical SJ precision
while vg mpmap reports ~3%. Four candidate fixes (partner-source, stitch-mismatch, partner-
uniqueness, partner-quality) plus overhang/read-count filters ALL failed to move mpmap's precision.
So we do not yet know the actual cause. This plan determines it mechanistically, so the eventual
fix is chosen from evidence rather than guessed — is it a cheap filter, multimapping-aware
reporting, or a splice-rescue re-architecture?

Reference numbers to reproduce/beat live in `$CLAUDE_JOB_DIR/tmp/chr20bench` (score_pc.py,
truth_pc.tsv, reads_pc*.fq, sj_lin_*.tsv). STAR = /usr/bin/STAR 2.7.11b.

**Substrate:** the goal graph is `refpath` — the HPRC chr20 pangenome pruned to the `CHM13#0#chr20`
reference path (`vg mod -k`), preserving pangenome node IDs (see `mpmap_vs_star_sj_STATUS.md`).
The 3% figure below was first measured on a sequence-equivalent `vg construct` reference graph
(a diagnostic); the exact `refpath` re-measurement is the authoritative number.

## STEP 0 — eliminate the benchmark confounder first (cheap, ~0.5 day)
The current designer exons are random CHM13 fragments that contain repeats; characterization
showed **73% of mpmap's false junctions are genome-wide, unique-read splices from those repeats**
(reads' Alu/LINE halves match elsewhere). Rebuild a CLEAN control before diagnosing mpmap:
- Screen candidate exon pairs so the 300 bp spliced construct is genome-UNIQUE: RepeatMasker/
  Dfam mask + a mappability/self-alignment check; require both 150 bp flanks AND the
  junction-spanning k-mers to occur once in CHM13 chr20.
- Re-run BOTH tools; recompute precision with score_pc.py.
- This PARTITIONS the 3%: if mpmap's precision jumps toward STAR's, most of the gap was the
  benchmark and the residual is the true algorithmic gap. If it stays low, the gap is all mpmap.
  Either way, all downstream diagnosis runs on the clean control.

## STEP 1 — the decisive experiment: read-level head-to-head diffing
For every FALSE junction mpmap reports that STAR does not, look at what STAR did with that
junction's supporting reads. Both tools emit per-read alignments; align them by read name.

1. Capture per-read decisions on the identical FASTQ:
   - STAR: `--outSAMattributes NH HI AS nM jM jI` + `SJ.out.tab`. `jM`/`jI` give each read's
     junction motif+coords; `NH` = number of reported loci (multimapping).
   - mpmap: `-F GAM` plus a per-read junction trace. This needs a small instrumentation add
     (below): tie each recorded junction to its read name, the chosen spliced score, and the
     best REJECTED alternative alignment score.
2. For each mpmap false junction, take its supporting read names; for each read, look up STAR's
   decision and classify the suppression mechanism:
   - (A) STAR did not splice the read at all → STAR's splice trigger / contiguous-vs-spliced
     margin is stricter than mpmap's.
   - (B) STAR spliced it at the TRUE junction, not the false one → alignment-SELECTION difference
     (STAR chose the better whole-read window).
   - (C) STAR mapped the read to multiple loci (NH>1) and its junctions are excluded from the
     unique-read tally → multimapping-aware reporting.
   - (D) STAR called the junction but dropped it via an `outSJfilter*` → a filter mpmap lacks.
3. Histogram (A)–(D) over all false junctions → the dominant cause(s). This is the deliverable.

## STEP 2 — enumerate the algorithmic differences, each with a falsifiable test
For each: STAR's mechanism | mpmap's | controlled test.

1. **Whole-read best-window selection** (leading hypothesis). STAR clusters seeds into genomic
   windows (`winBinNbits`, `winAnchorDistNbins`, `seedPerWindowNmax`), stitches within each
   window, and reports the junction only from the single highest-scoring window for the WHOLE
   read. mpmap anchors one exon and rescues the soft-clip tail to any scoring partner, accepting
   a locally-optimal splice. TEST: instrument mpmap to also compute the read's true-junction
   alignment score; count reads where the true alignment scores ≥ the chosen false splice but was
   not selected. If large → this is the cause.
2. **Multimapping-aware junction reporting.** STAR `outFilterMultimapNmax` / `winAnchorMultimapNmax`
   + `NH`; junctions from multimapping reads don't count as unique support. mpmap splits on
   `multiplicity < 1.5`. We already saw mpmap calls the false-junction reads UNIQUE — confirm STAR
   calls the SAME reads multimapping (NH>1). If so, mpmap's multiplicity estimate is the lever.
3. **Splice-vs-contiguous margin.** STAR only splices when the spliced score beats the best
   contiguous alignment by the junction penalty. TEST: for false-junction reads, does a contiguous
   (unspliced) alignment exist that scores comparably and mpmap ignored?
4. **`outSJfilter*` not yet tried.** `outSJfilterDistToOtherSJmin` (10 0 5 10),
   `outSJfilterIntronMaxVsReadN`, `outSJfilterCountUniqueMin` per motif class, `alignSJoverhangMin`.
   TEST: port each as a POST-HOC filter on mpmap's `--sj-out` (cheap, no compile); measure
   precision/recall deltas. (Note: overhang≥30 and unique≥3 already tested, did not help.)
5. **Motif scoring** — STAR flat `scoreGapNoncan=−8` vs mpmap log-frequency. Already RULED OUT as
   the precision lever (document as a negative result, don't re-test).

## STEP 3 — instrumentation to build (small)
- mpmap: a debug `--sj-out` mode that also emits, per recorded junction, the supporting read
  names and, per read, `chosen_score` and `best_alternative_score`. Reuses the existing mpmap_sj
  collector + the splice-rescue site in multipath_mapper.cpp.
- A join script keyed on read name that, per false junction, emits STAR's class (A–D) and score
  deltas. Reuse score_pc.py's truth-matching.

## Deliverable
A one-page table: the distribution of STAR's suppression mechanism (A–D) across mpmap's false
junctions, on BOTH the repeat-heavy and the clean control. This states definitively whether the
fix is (i) a benchmark artifact, (ii) a portable `outSJfilter`, (iii) multimapping-aware
reporting, or (iv) whole-read best-window selection (the re-architecture). Only then do we choose
an implementation — see `seed_pair_splice_generation_plan.md` for the re-architecture option if
(iv) dominates.

## Effort
~2–3 days: clean control (0.5d) + per-read capture & join (1d) + classification & write-up (1d).
Everything runs on the existing chr20 harness; no full pangenome runs required for the diagnosis.
