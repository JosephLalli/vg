# Phase 4: de novo non-canonical splice discovery on the graph — spec

> **Navigation:** Hub / entry point for all splice/SJ docs: [`mpmap_vs_star_sj_STATUS.md`](mpmap_vs_star_sj_STATUS.md).
> Sequence position: follows `whole_read_best_window_plan.md` (M1/M2 shipped) and
> `star_parity_graph_spliced_alignment_plan.md` (Phases 1/2/3/6 design); this doc is Phase 4.

> **SUPERSEDED (2026-07-10/11) — kept for history, not the mechanism that shipped.** The de novo
> discovery goal spec'd here WAS met, but not by either plan in this doc. Phase 4-lite (the
> de-gated-motif + whole-read-gate design below) was implemented as `--splice-denovo`
> (commit `226a273`) and then measured **inert** by ablation: local gate + a raised
> `--max-motif-pairs` budget reproduces `--splice-denovo`'s output byte-for-byte. Phase 4 full (the
> stitch-first splice-aware DP, second half of this doc) was **never built** — no
> `banded_global_aligner` surgery happened. The actual, verified mechanism was raising the
> pre-existing `--max-motif-pairs` budget (200 -> 20000; the flag already existed upstream) plus
> supplying all-256 custom motifs via `--splice-motif-scores` and filtering with `--sj-min-unique`.
> See `beat_star_splice_discovery_implementation.md` ("BREAKTHROUGH" and both retrospective
> sections) for the full account and numbers. The rest of this document describes the
> superseded plan as originally written, unedited, for the historical record.

> **Status (2026-07-10):** SPEC (both a bounded "lite" version and the full stitch-first DP). Follows
> `whole_read_best_window_plan.md` (M1/M2 shipped) and `star_parity_graph_spliced_alignment_plan.md`.
> Confirmed by reevaluation: Phases 1/2/6 are no-ops on the discovery gap (they refine
> scoring/placement/reporting of *already-proposed* junctions); discovery is a candidate-**generation**
> problem, which is what Phase 4 addresses.

## The gap: generation vs selection
- **Selection** (which proposed junction to keep) — solved by M1 (`--splice-eval-all`, no pruning) +
  M2 (`--splice-whole-read`, whole-read re-score + fixed motif bonus).
- **Fidelity** (correct coords/motif of a kept junction) — Phases 1 (variant-aware re-score), 2
  (`--sj-slide` canonicalization), 6 (calibration/filters).
- **Generation** (proposing a junction at an *unknown-motif* position) — **unaddressed**. mpmap's
  `SpliceRegion` only proposes positions matching a *supplied* motif, and `SpliceStats::motif_score`
  only scores listed motifs. A junction whose motif you did not pre-list is never proposed.
- Evidence: non-canonical recall 25/30 **only** with the 30 exact truth motifs supplied; with all 256
  dinucleotide pairs listed (log-frequency), recall = **0%**. Phases 1/2/6 confirmed metric no-ops.

## Why the earlier all-256 attempt gave 0% (the key lesson for the gate)
A real non-canonical motif is ~3000× rarer than GT-AG, so its log-frequency score is ≈ −8, and mpmap's
**local** connecting-alignment gain (only the ~16 bp trimmed window) cannot overcome −8 → the
significance gate (`net_score > no_splice_log_odds`) fails → 0% discovery. STAR uses the *same*
`scoreGapNoncan = −8`, but STAR scores the junction against the **whole-read** alignment gain (~44 bp
of the recovered exon), which dwarfs −8. **Lesson:** the penalty magnitude is fine; the missing piece
is gating on the *whole-read* gain, not the *local* one — and M2 already computes the whole-read score.
So Phase 4-lite is not just "list more motifs"; it is "de-gate the motif **and** gate on whole-read".

## Phase 4-lite — de-gate the motif + whole-read gate  [MODERATE effort/risk; recommended first]
Three changes; reuses M1/M2 for selection.

1. **`SpliceStats` (`src/splicing.cpp` / `.hpp`):** register **all 256 dinucleotide pairs** as motifs.
   Listed motifs keep their supplied score; unlisted pairs get a **flat graph-native `scoreGapNoncan`**
   (a fixed penalty in mpmap's aligner score units — see Phase 6 calibration), not a log-frequency.
   `motif_score(idx)` returns the flat penalty for unlisted pairs. This makes *every* dinucleotide a
   scorable motif rather than a gate.

2. **`SpliceRegion` (`src/splicing.cpp`):** add an all-position candidate mode — every position in the
   `2×overhang` window is a candidate splice site (each position has *some* dinucleotide), so the true
   junction is proposed regardless of motif. Bounded (~30 bp window); cap with the existing
   `max_motif_pairs` budget. (Enumeration is over listed motifs today via `candidate_splice_sites`.)

3. **Whole-read significance gate (`test_splice_candidates`, `src/multipath_mapper.cpp`):** move the
   accept/reject decision from the **local** `net_score` to the **whole-read** score. Concretely: apply
   a relaxed local pre-filter to bound cost, then for surviving candidates compute M2's `whole_read_score`
   (already implemented) and gate on `whole_read_score > no_splice_log_odds`, so the flat −8 is dwarfed
   by the full-read alignment gain instead of the ~16 bp local gain. M2's re-rank then selects the winner.

- **Flag:** `--splice-denovo` (implies `--splice-whole-read`; default-off, `--sj-out` byte-identical).
- **Validation:** a control with **unlisted** non-canonical motifs — map supplying only GT-AG/GC-AG/AT-AC
  in the motif file and measure non-canonical recall. **Success = recall > 0 without the motif list**,
  approaching the 25/30 listed-motif recall, with precision/runtime held (lean on M2 + Phase 3 paralog/NH
  for precision).
- **Effort/risk:** moderate. Main risk is runtime (all-position × whole-read gate) — bound by the window
  size, the `max_motif_pairs` budget, and evaluating only splice-rescue-triggering reads.

## Phase 4 full — stitch-first splice-aware graph DP  [HIGH effort/risk; fallback]
Needed only if, after 4-lite, junctions are discovered but systematically **mis-placed** or missed
because the anchor-then-rescue structure biases placement (i.e. the discrete propose-then-align
pipeline can't reach STAR's accuracy). Replace rescue with a native spliced alignment:

1. **Spliceable subgraph assembly:** after clustering (existing), gather spliceable neighbor subgraphs
   within the max-intron **graph distance** (SnarlDistanceIndex — the graph analog of STAR's genomic
   windows `winBinNbits`/`winAnchorDistNbins`) into one alignable subgraph, **retaining variant nodes**.

2. **Splice-gap DP transition:** extend the graph aligner (gssw / xdrop path) with a new transition — a
   **splice-gap edge** between motif-bearing graph positions, scored `flat-motif-penalty +
   intron-length-prior`, alongside match/mismatch/indel. This is the real surgery: a new DP state and
   traceback over the subgraph.

3. **Whole-read stitch:** one DP aligns the whole read across the spliceable subgraph; splices are
   inserted natively where the DP prefers them — **subsuming** soft-clip→rescue→M1/M2 and Phases 1–2
   (variant-awareness and placement become properties of the DP).

4. **Multimapping/output:** feed unique/multi and NH from the DP's alternative traces (ties to Phase 3).

- **Effort/risk:** high (aligner surgery; DP cost scales with spliceable-subgraph size). Gate strictly;
  validate exhaustively against the 4-lite result it replaces.

## Decision criterion (4-lite → full)
Run 4-lite on the unlisted-motif control. If non-canonical recall approaches the listed-motif 25/30 at
acceptable precision and runtime → **4-lite suffices, full DP unnecessary**. If 4-lite discovers but
**systematically mis-places** the junction (needs the local rescue structure removed) or is too slow →
**escalate to the full DP**. This is why 1/2/6 came first: they remove the confounds (fidelity) so the
4-lite result cleanly isolates whether the DP is forced.

## Graph invariant (retained throughout)
Candidates are graph positions; the flat penalty scores graph dinucleotides; the whole-read score aligns
over graph subgraphs (Phase 1 variant-aware); discovered junctions become graph edges (Phase 5). Nothing
is flattened to a linear sequence.

## Risks / open questions
- **4-lite runtime:** all-position enumeration × whole-read gate. Bound by window size, `max_motif_pairs`,
  and splice-trigger-only evaluation; profile before shipping the default.
- **Precision:** de-gating admits more candidates → more potential false positives; control via M2's
  whole-read selection and Phase 3 (anchor-frequency / NH), not by re-gating on motif.
- **Calibration coupling:** the flat `scoreGapNoncan` must be in mpmap aligner units (Phase 6) and used
  **consistently** in both the gate and the M2 re-rank, or the two disagree.
