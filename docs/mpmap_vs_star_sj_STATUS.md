# mpmap vs STAR on splice-junction detection — STATUS (authoritative)

**Entry point for all splice/SJ docs in `docs/`.** A freshly-spawned agent should start here. The
Document Map (bottom of this file) lists every related planning doc and links forward to each one;
each planning doc links back here.

Single source of truth for the current goal and standing. A **Document Map** of every related doc,
and a **verified flag inventory**, are at the bottom. Last updated 2026-07-11.

> **Superseded section below (kept for history):** everything from "Current goal" through "Next
> step — implement the fix" describes the state as of 2026-07-10, before de novo discovery was
> attempted. See **`beat_star_splice_discovery_implementation.md`** for the de novo discovery
> narrative and **`mpmap_minimal_branch.md`** for the current, authoritative result: de novo
> non-canonical discovery was NOT solved by aligner surgery (Phase 4 / the stitch-first DP was never
> built); it was solved by raising the pre-existing `--max-motif-pairs` budget plus supplying custom
> motifs (`--splice-motif-scores`) and a support filter (`--sj-min-unique`).
>
> **Correction (2026-07-11):** the "25/30 @ 52%" figure below (this doc's "Current standing" table,
> M2-only, no budget lever) **understated mpmap** — it never applied the pre-existing
> `--max-motif-pairs` budget lever to the `refpath` genome-unique (45-junction) control it was run
> on. Re-measured on that same control with the budget lever applied (`--splice-motif-scores` +
> `--max-motif-pairs 20000` + `--sj-min-unique 3`): **canonical 15/15 @ 100%, non-canonical 30/30 @
> 97%** (Lean recipe); adding `--splice-denovo` brings non-canonical to **30/30 @ 100%**, exactly
> tying STAR. STAR pass-1 on the same control: canonical 12/15 @ 57%, non-canonical 24/30 @ 100%.
> mpmap beats STAR on canonical recall and precision, ties/exceeds on non-canonical recall, and with
> `--splice-denovo` ties STAR's non-canonical precision. **Non-canonical precision is no longer an
> open gap on this control.** (The separate "~93-94% non-canonical precision" figure elsewhere in
> `beat_star_splice_discovery_implementation.md` is from a larger, denser 465/1282-junction control —
> a different control, not directly re-measured this session; see that doc's scope note.) See
> `mpmap_minimal_branch.md` for the full table, runtime cost, and the corrected characterization of
> `--splice-denovo` (previously described as "inert"; that finding was regime-specific — see that
> doc). Real-data (annotated-graph) junction recovery via surjection remains a **93-96% tie** with
> STAR, a different regime from de novo discovery — not a win, not a loss, and not affected by this
> correction.

## Current goal (as of 2026-07-10 — see superseded note above)
**Adapt STAR's algorithm to the node/edge graph and match or beat it on splice-junction detection.**
The read-by-read precision diagnostic is complete; the whole-read best-window re-score (M2,
`--splice-whole-read`, commit `8dfa2db`) and a graph-native paralog filter (`--sj-anchor-multimap-max`
+ `--sj-min-unique`, commit `e272e6d`) have shipped. The active line of work is the STAR-parity
roadmap (`star_parity_graph_spliced_alignment_plan.md`) — closing the remaining gaps while keeping the
node/edge graph model, with de novo non-canonical discovery (`graph_denovo_splice_discovery_plan.md`,
Phase 4) as the next lever.

**The benchmark it serves:** get **either vg mpmap seeding mode — MEM or MMP — to mirror STAR's
splice-junction performance on a linear chr20**. "Linear chr20" here means the HPRC chr20 pangenome
**stripped of every non-reference node and path** — vg maps fine against a pangenome reduced to the
reference alone (see **Substrate** below). Concretely: make **vg mpmap perform as well as or better
than STAR on BOTH precision and recall** of splice-junction detection — **canonical and
non-canonical** — on that reference-only chr20 graph **with introduced non-canonical junctions**.
Either seeding pathway is acceptable (seeding has been shown not to be the lever). Two gaps remain
open: **non-canonical precision** (mpmap 52% vs STAR 100% on the clean control — a
paralog-disambiguation problem, roadmap Phase 3) and **de novo non-canonical discovery** (mpmap only
discovers junctions whose motif is pre-listed — Phase 4). The precision diagnostic
(`star_vs_mpmap_sj_precision_diagnostic_plan.md`) has delivered its verdict (whole-read best-window),
so it is complete rather than active.

**Outcome (2026-07-10/11, see `mpmap_minimal_branch.md`):** de novo discovery was closed via the
`--max-motif-pairs` budget lever, not Phase 4's planned stitch-first DP (that DP was never built —
see the superseded note above). With the budget lever correctly applied to the `refpath`
genome-unique (45-junction) control, non-canonical precision is **97%** (Lean recipe) to **100%**
(with `--splice-denovo`, exactly tying STAR) — no longer a metric below STAR on that control; an
earlier measurement on the same control that omitted the budget lever had understated this. The
larger 465/1282-junction scaled control's ~93-94% figure (`beat_star_splice_discovery_implementation.md`)
was not re-measured with this correction. See the correction note above and `mpmap_minimal_branch.md`
for the full table.

**Substrate definition (per user, 2026-07-10):** the "linear graph" is NOT a freshly-constructed
reference. It is the **HPRC chr20 pangenome pruned to the reference haplotype** — keep only the
nodes and paths that define `CHM13#0#chr20`, prune every non-reference node/path
(`vg mod -k CHM13#0#chr20 maptarget.pg`). This **retains the pangenome's node IDs/boundaries and
any annotated CAT junction edges** on the reference, unlike `vg construct` which re-chops the same
sequence into fresh nodes with no junction edges. Testing is on chr20.

## Benchmark (the standing measurement)
- **Graph:** `refpath` = HPRC chr20 pangenome (`maptarget`, CHM13-primary) pruned to the
  `CHM13#0#chr20` reference path via `vg mod -k`; pangenome node structure preserved, all
  non-reference nodes/paths removed. Node→CHM13 coordinate scoring reuses `node2chm13_mt.tsv`
  (same node IDs). Head-to-head with STAR on the identical CHM13 chr20 sequence.
  (**The standing table below is measured on `refpath`** — the authoritative substrate — over the
  3,600-read subset. An earlier `vg construct` reference graph, sequence-identical but with fresh
  node boundaries and no junction edges, gave **equivalent** results as a diagnostic: precision 3%,
  non-canonical recall 17/30. So the precision gap is **not** an artifact of graph construction —
  the pangenome node structure + junction edges moved precision only 2%↔3%.)
- **Positive control:** 15 canonical (GT-AG) + 30 non-canonical junctions with *verified, diverse*
  non-canonical motifs, 150 bp exon anchors, ~800 bp introns, deep tiled reads (400/junction,
  1% error). Scored **motif-verified** against exact reference coordinates.
- **Read source is bulk RNA-seq, not single-cell.** The control reads (`reads_pc*.fq`, 100 bp
  single-end simulated cDNA) are bulk-style fragments — no barcodes/UMIs. Splice detection depends
  only on the cDNA read, so a 10x scRNA source (whose R1 barcode/UMI is discarded before mapping)
  would add nothing; bulk is the simpler, apples-to-apples source against STAR (natively a bulk
  aligner). The 10x `gex` reads under `hprc_v2_vg_rna/chr20_compare/` are a *separate* real-data
  sanity check, not the controlled benchmark.
- **Reference:** STAR pass-1 (`/usr/bin/STAR` 2.7.11b), de novo.
- **Harness:** `$CLAUDE_JOB_DIR/tmp/chr20bench` — `score_pc.py`, `truth_pc.tsv`, `reads_pc*.fq`,
  `design_pc.py`, `motif_curated.txt`, `sj_lin_*.tsv`. (Job scratch is ephemeral; the numbers below
  are the durable record.)

## Current standing (2026-07-10, `refpath`, donor fix + M2 whole-read re-score)
> **This table's config does not include the `--max-motif-pairs` budget lever or `--sj-min-unique`
> filtering — see the correction note at the top of this doc.** It is kept verbatim below as the
> historical record of the M2-only measurement. For the authoritative current numbers (budget lever
> applied, `--sj-min-unique 3`), see `mpmap_minimal_branch.md`'s results table: Lean recipe
> non-canonical 30/30 @ 97%, `--splice-denovo` 30/30 @ 100%, both beating or tying STAR on every
> metric.

The precision diagnostic (STEP 0) removed **two measurement artifacts** — a benchmark repeat
confounder and a `--sj-out` donor-coordinate bug — and a scorer motif-strand convention; M2
(`--splice-whole-read`, commit `8dfa2db`) adds whole-read best-window re-scoring. All cells are the
same 3,600-read subset through both tools on the **clean genome-unique control** (authoritative).

| Metric                        | mpmap default | mpmap `--splice-whole-read` | STAR    | Goal status |
|-------------------------------|---------------|------------------------------|---------|-------------|
| Mapping accuracy (≤100 bp)    | 93%           | 93%                          | ≈93%    | **MET**     |
| Canonical SJ recall           | 14/15         | 14/15                        | 12/15   | **MET (beats STAR)** |
| Non-canonical SJ recall       | 25/30         | 25/30                        | 24/30   | **MET (beats STAR)** |
| Non-canonical SJ precision    | 50% (30/60)   | **52%** (30/58)              | 100% (24/24) | superseded — see note above; with the budget lever this is 97-100%, not open |
| MEM vs MMP (any metric)       | identical     | —                            | —       | seeding is not a lever |

M2 clears the no-regression gate (every metric ≥ default on the clean AND repeat-heavy controls;
default-off byte-identical; tests 33/35 pass) and improves non-canonical precision. The residual gap
to STAR's 100% is **paralog/repeat false positives** that align well to their own (wrong) two-exon
graph — the whole-read score cannot reject these; that is a paralog-disambiguation problem, separate
from splice placement (`whole_read_best_window_plan.md`).

Repeat-heavy control (fixed binary), for reference: mpmap 20/30 recall, **2%** (22/891) precision;
STAR 10/30, 91%. The low precision there is genuine repeat-mismap false junctions (reads' Alu/LINE
halves anchoring elsewhere), not the algorithm — so the clean control is authoritative. Note the
earlier "mpmap non-canonical recall 14/30 **exceeds** STAR 10/30" was a repeat-heavy-benchmark
artifact; on clean data it is a near-tie (23 vs 24).

## What is done
- **Genome-unique positive control** (STEP 0, `design_pcU.py`): jellyfish k=50 canonical k-mer
  uniqueness screen (98.6% of chr20 50-mers are unique) requiring every exon-flank 50-mer to occur
  exactly once in CHM13 chr20 and every junction-spanning 50-mer zero times. Removes the repeat
  confounder — ~92% of the pre-fix false non-canonical junctions were repeat-driven mismaps.
- **`--sj-out` donor coordinate fix** (`multipath_mapper.cpp`): the donor was recorded at the START
  of the last donor-side connecting-alignment block (`dp.offset()`) instead of the splice point (its
  END). This shifted the reported donor ~-16 bp (sequence-independent: canonical GT-AG junctions,
  which mpmap places correctly, all landed at -16 while STAR nailed them) and — because the block
  length varied per read — split each true junction into ~7 phantom records. It corrupted **both**
  precision (phantom false positives) and recall (true detections fell outside the ±15 scoring
  window). Fixed to `dp.offset() + mapping_from_length(dm)`; donor offset now 0. Tests `33`+`35`
  pass (56/56); only the `--sj-out` donor column changes, default-off byte-identical.
- **Relaxed splice-motif frequency budget** (committed `9baee7e`): per-motif frequencies as
  independent log-odds priors (sum may exceed 1). Non-canonical recall **0/30 → 23/30** on the clean
  control, ~tying STAR's 24/30. The **recall half of the goal is MET.**
- **`--sj-out max_overhang` populated**; **`--mmp-splice-pairs`** experimental precision levers
  (all default-off byte-identical; none recover precision).

## What is open — non-canonical PRECISION (residual)
**(2026-07-10 framing, superseded by the de novo work — see the note at the top of this doc.)**
The only unmet metric, now much smaller: mpmap 47% vs STAR 100% on the clean control. The residual
false calls are **low-support (median 1 read) ±1-3 bp positional/motif duplicates of TRUE
junctions** — 30/32 within 200 bp of a true site, 24/32 share both endpoints with a true junction
but carry a shifted wrong-motif label. Filter behavior: a distance-collapse (STAR
`outSJfilterDistToOtherSJmin` analogue, keep highest-support within W) lifts precision **47%→63%**
at W≥8 but costs recall **23→19**; overhang and unique-read filters do not separate true from false
(both mostly singletons, both high overhang). The residual gap is the **whole-read best-window**
architectural difference: STAR reports one splice per read cluster; mpmap's splice rescue reports
per-read positional/motif variants. (This same diagnosis — per-read junction fragmentation with no
cross-read consensus — is what still limits non-canonical precision at scale in the current, de
novo-discovery result; see `beat_star_splice_discovery_implementation.md` and
`whole_read_best_window_plan.md` M3, unimplemented.)

## STEP 1 result — mechanism confirmed: whole-read best-window (2026-07-10)
Added per-read `--sj-reads` instrumentation (junction → supporting read names + chosen splice score;
default-off, tests pass) and diffed mpmap's 32 clean-control false non-canonical junctions against
STAR's per-read `jM`/`jI`/`NH`. Of the 429 (false-junction, supporting-read) pairs:

| STAR did with the read | share | meaning |
|------------------------|-------|---------|
| **B — spliced at the TRUE junction** | **89%** | STAR placed the *same read* at the correct junction |
| A — did not splice | 6% | splice-margin |
| C — multimapping (NH>1) | 1% | not multimapping |
| D — spliced then SJ-filtered | 0% | not a missing `outSJfilter` |

**Verdict:** the residual precision gap is an alignment-**selection** defect. For 89% of the reads
behind an mpmap false junction, a correct whole-read placement exists and STAR takes it; mpmap's
splice rescue anchors one exon and accepts a locally-optimal soft-clip donor instead. Not seeding,
not multimapping, not a filter. The lever is **whole-read best-window selection** — score the chosen
splice against the read's best alternative placement and report only the winner (STAR's architecture).

## Next step — implement the fix
**(2026-07-10 framing; see `beat_star_splice_discovery_implementation.md` for what was actually
done next — the de novo discovery work, not this precision fix directly.)**
Choose per review: (a) **whole-read best-window** in splice rescue (the real lever, larger change:
compare the rescued spliced alignment to the read's best alternative and keep the whole-read
optimum), or (b) ship the portable **distance-collapse SJ filter** (`outSJfilterDistToOtherSJmin`
analogue) as an interim precision option (47%→63%, recall 23→19).

**Superseded (2026-07-11):** the "~93-94% vs STAR ~100%" figure below was measured without the
`--max-motif-pairs` budget lever applied. Re-measured with the lever applied (`--max-motif-pairs
20000` + `--sj-min-unique 3`): Lean recipe non-canonical precision is **97%**, and `--splice-denovo`
closes it to **100%**, exactly tying STAR — see `mpmap_minimal_branch.md`. Non-canonical precision is
no longer an open item on the genome-unique control. The per-read junction fragmentation diagnosis
below may still explain the residual ~3% at Lean-only (pre-`--splice-denovo`), but the
whole-read-cluster consensus fix (`whole_read_best_window_plan.md` M3) is no longer necessary to
close the gap to STAR, since `--splice-denovo` already does.

**(Original 2026-07-10/11 framing, kept for history):** non-canonical precision at scale (~93-94% vs
STAR ~100%) is diagnosed as per-read junction fragmentation — the SJ sink keys on exact node/offset,
so one true junction with per-read donor/acceptor/motif jitter fragments into several low-support
rows. Closing it needs the whole-read-cluster / one-junction-per-cluster consensus fix
(`whole_read_best_window_plan.md` M3), which is **not implemented**.

## Flag & feature inventory (verified against `src/subcommand/mpmap_main.cpp`, 2026-07-10)
All flags below are default-off (or default-value byte-identical); default mapping/splice output is
unchanged when unused. Defaults noted where non-obvious.

- **MMP seeding (STAR-style front end)** — `mmp_seeding_implementation_plan.md`, `star_first_pass_plan.md`:
  `--mmp-seed` (splice-rescue MMP re-seed), `--mmp-primary` (MMP replaces the MEM pool),
  `--mmp-augment` (MMP appended to MEM pool), `--mmp-chain` (sequential MMP walk),
  `--mmp-strand-mode {native|rc|both}`; density/bound knobs `--mmp-min-prefix`, `--mmp-hit-max`,
  `--mmp-max-intron`, `--mmp-max-seeds`, `--mmp-start-lmax`, `--mmp-start-lmax-over-lread`,
  `--mmp-seed-per-read-max`; `--mmp-relax-accept N` (relaxed short-overhang acceptance, MMP candidates only).
- **Splice scoring** — `mmp_star_parity_plans.md` (Plan B), `star_first_pass_plan.md` (item 6):
  `--splice-motif-scores FILE` (per-motif donor/acceptor frequency table; **the relaxed-budget recall lever**),
  `--sjdb-score N` (bonus for annotated/graph-edge junctions; partial).
- **Whole-read best-window re-scoring** — `whole_read_best_window_plan.md`:
  `--splice-eval-all` (disable pruning: evaluate all candidate splice windows, not just the top-scoring
  anchor; implied by `--splice-whole-read`; default off),
  `--splice-whole-read` (whole-read best-window re-score — the M2 precision lever; default off;
  implies `--splice-eval-all`),
  `--splice-whole-read-context INT` (bp of read context used for re-score window; 0 = auto, uses
  read length; default 0),
  `--splice-whole-read-topk INT` (top-K candidate windows considered per re-score; default 8),
  `--splice-whole-read-motif-weight FLOAT` (weight of motif log-odds in the whole-read score;
  default 0.5).
- **De-novo high-accuracy mode** — `--splice-denovo` (implies `--splice-eval-all` +
  `--splice-whole-read`, auto-raises `--max-motif-pairs` 200 -> 20000; default off). **Positioning
  (2026-07-11, corrected):** this is an opt-in high-accuracy de-novo mode, not a required step. The
  **Lean recipe** (`--splice-motif-scores` + `--max-motif-pairs 20000` + `--sj-min-unique 3`, no
  `--splice-denovo`) is the recommended default path — it beats STAR on 3 of 4 metrics and ties the
  4th at only **+12% runtime** over stock mpmap (~7,087 vs ~7,930 reads/s, 180k splice-heavy reads,
  `refpath`, `-t 16`). `--splice-denovo` closes the Lean recipe's remaining non-canonical precision
  gap (97% -> 100%, exactly tying STAR) but costs **~2.0x runtime vs Lean** (~3,371 reads/s; 2.35x
  vs stock) on splice-heavy data, because it disables the branch-and-bound early-out and re-aligns
  the whole read across a donor/acceptor graph for each of the top-`--splice-whole-read-topk`
  (default 8) candidates. The cost is confined to the de-novo splice-**rescue** path (reads that
  never trigger rescue pay nothing), so real-world overhead on an annotated pangenome — where most
  spliced reads cross already-annotated edges — is expected to be much smaller than this
  splice-heavy synthetic figure. `--splice-whole-read-topk` is the available runtime lever if the 2x
  cost needs to be cut. This corrects the earlier "the whole-read gate is inert" finding
  (`beat_star_splice_discovery_implementation.md` "BREAKTHROUGH" section): that ablation used
  all-256 flat motifs with a manually-raised budget; in the curated/custom-motif regime the gate is
  load-bearing for the last precision gap. Full numbers, the runtime ladder, and the minimal
  two-commit extraction of this recipe: `mpmap_minimal_branch.md`.
- **Junction output** — `star_first_pass_plan.md` (item 7):
  `--sj-out FILE` (graph-native SJ table: donor/acceptor `node:offset:strand`, motif, annotated flag,
  unique/multi read support, max overhang; donor coordinate fixed to the splice point
  `dp.offset() + mapping_from_length`). **(2026-07-11, commit `59df9e4`)** the table now also appends
  `donor_ref_path`, `donor_ref_pos`, `acceptor_ref_path`, `acceptor_ref_pos` — each junction endpoint
  projected onto a reference/generic path via `algorithms::nearest_offsets_in_paths`, so the table is
  self-sufficient in linear coordinates without an external node->coordinate map or surjection.
  Endpoints with no reference path fall back to `.` in all four columns. Haplotype-path placement
  (e.g. reporting on a per-sample path like `HG002#1#...`) is deferred pending a haplotype-carrying
  spliced graph — the current spliced graph embeds only reference paths. Node columns and the
  recorded-junction logic (rescue-path only, see below) are unchanged.
  `--sj-reads FILE` (debug: per-junction supporting read names + chosen splice score; drove the STEP 1
  whole-read-best-window verdict),
  `--sj-candidates FILE` (debug: dump all candidate splice windows before selection; for Phase 4
  discovery analysis),
  `--sj-min-unique INT` (filter `--sj-out` rows whose unique-read support is below this threshold;
  default 0 = no filter),
  `--max-splice-overhang INT` (maximum overhang reported in `--sj-out`; default
  `2 * max_softclip_overlap` = 16).
  **Standing scope limit (unchanged, deliberate — not built this session):** `--sj-out` still records
  only de-novo splice-**rescue** junctions (`mpmap_sj::record`, called solely from the rescue path in
  `multipath_mapper.cpp`). A read that crosses an already-annotated graph splice edge during normal
  chaining never enters rescue and is never logged. On an annotated graph this is the majority of
  spliced reads (~90%+); for real-data / annotated-graph junction completeness, use surjection
  (`vg surject -S` -> CIGAR `N` ops) instead of `--sj-out`. See
  `beat_star_splice_discovery_implementation.md`'s "Real-data validation" section.
- **Junction-table multi-mapping controls** — `star_parity_graph_spliced_alignment_plan.md`:
  `--sj-anchor-multimap-max INT` (reads with more than this many anchor mappings are counted as
  multi- rather than uniquely-mapping in `--sj-out`; 0 = off; default 0),
  `--sj-slide` (canonicalize junction coordinates by sliding to the nearest annotated position when
  the sequence is compatible; default off).
- **Experimental precision (open work)** — `seed_pair_splice_generation_plan.md`:
  `--mmp-splice-pairs` (seed-pair-gated candidates + non-canonical partner constraints; does NOT yet recover precision).
- **Proposed but NOT shipped** (do not cite as current): `--mmp-extend` (built then removed — degraded mapping),
  `--mmp-both-strands`, `--sj-ref-path`, `--sj-min-overhang`, `--sj-min-reads`, `--sjdb-overhang-min`,
  `--novel-overhang-min`, `--splice-motif-preset`.

## Document map (all splice/SJ docs, 2026-07-11)
| Doc | Role / phase | Status | Substrate |
|-----|--------------|--------|-----------|
| **`mpmap_vs_star_sj_STATUS.md`** (this) | current goal + standing; the hub / entry point | authoritative for navigation; see `mpmap_minimal_branch.md` for the current, corrected result | chr20 `refpath` |
| `mpmap_minimal_branch.md` | corrected results table (budget lever applied), runtime ladder, and the minimal two-commit branch extraction (`mpmap-noncanonical-splice-minimal`) | **authoritative for current numbers** — supersedes the "25/30 @ 52%" / "~93-94% non-canonical precision" figures elsewhere in this doc and in `beat_star_splice_discovery_implementation.md` | chr20 `refpath` |
| `beat_star_splice_discovery_implementation.md` | de novo discovery execution plan + result + retrospectives | authoritative for the de novo/real-data discovery *narrative*; its headline precision numbers and "whole-read gate is inert" retrospective are corrected by `mpmap_minimal_branch.md` | chr20 `refpath` |
| `whole_read_best_window_plan.md` | M1/M2 whole-read best-window re-score design + success criteria | M2 shipped (`--splice-whole-read`, commit `8dfa2db`); M3 (cluster consensus) not built, and no longer necessary to reach STAR parity — `--splice-denovo` closes the gap instead (see `mpmap_minimal_branch.md`) | chr20 `refpath` |
| `star_parity_graph_spliced_alignment_plan.md` | graph-native STAR-parity alignment phases (Phases 1/2/3/6) | proposed; Phase 4 (stitch-first DP) superseded, not built — see below | chr20 `refpath` |
| `graph_denovo_splice_discovery_plan.md` | Phase 4 de novo non-canonical splice discovery spec (lite + full stitch-first DP) | **superseded** — de novo discovery was solved by the `--max-motif-pairs` budget lever plus custom motifs (`beat_star_splice_discovery_implementation.md`, `mpmap_minimal_branch.md`); the full stitch-first DP in this spec was never built. Its "measured inert" characterization of the Phase-4-lite whole-read gate is itself superseded — see `mpmap_minimal_branch.md`, that finding was regime-specific (all-256 flat motifs, manually-raised budget); in the curated-motif regime the gate (`--splice-denovo`) is load-bearing | chr20 `refpath` |
| `star_vs_mpmap_sj_precision_diagnostic_plan.md` | precision diagnostic (STEP 1 read-level analysis) | complete — verdict delivered (whole-read best-window) | chr20 `refpath` |
| `seed_pair_splice_generation_plan.md` | seed-pair precision approach | **REFUTED** | chr20 |
| `mmp_seeding_implementation_plan.md` | foundational MMP-seeding impl (M0–M7) | implemented | MHC (earlier) |
| `mmp_star_parity_plans.md` | follow-ups: A chaining / **B motif (recall win)** / C RC tail | implemented | MHC (earlier) |
| `star_first_pass_plan.md` | STAR first-pass items 2/3/6/7/5 (ships `--sj-out`) | implemented | MHC (earlier) |

**Substrate note:** the three implementation docs benchmarked on the MHC pangenome fixtures
(`panSC/tests/fixtures/mhc/sampleA.spliced`, 466-hap). Those numbers predate the move to chr20, which
was adopted because MHC paralogy confounded coordinate precision/recall. **chr20 `refpath` is now the
authoritative benchmark substrate.**
