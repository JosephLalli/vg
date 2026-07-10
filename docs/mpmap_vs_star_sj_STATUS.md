# mpmap vs STAR on splice-junction detection — STATUS (authoritative)

Single source of truth for the current goal and standing. A **Document Map** of every related doc,
and a **verified flag inventory**, are at the bottom. Last updated 2026-07-10.

## Current goal
**Continue the splice-search instrumentation analysis.** This is the active line of work.

**The benchmark it serves:** get **either vg mpmap seeding mode — MEM or MMP — to mirror STAR's
splice-junction performance on a linear chr20**. "Linear chr20" here means the HPRC chr20 pangenome
**stripped of every non-reference node and path** — vg maps fine against a pangenome reduced to the
reference alone (see **Substrate** below). Concretely: make **vg mpmap perform as well as or better
than STAR on BOTH precision and recall** of splice-junction detection — **canonical and
non-canonical** — on that reference-only chr20 graph **with introduced non-canonical junctions**.
Either seeding pathway is acceptable (seeding has been shown not to be the lever); the one open gap
is **non-canonical precision**, which the instrumentation analysis
(`star_vs_mpmap_sj_precision_diagnostic_plan.md`) is built to diagnose read-by-read.

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
The precision diagnostic (STEP 0) removed **two measurement artifacts** — a benchmark repeat
confounder and a `--sj-out` donor-coordinate bug — and a scorer motif-strand convention; M2
(`--splice-whole-read`, commit `8dfa2db`) adds whole-read best-window re-scoring. All cells are the
same 3,600-read subset through both tools on the **clean genome-unique control** (authoritative).

| Metric                        | mpmap default | mpmap `--splice-whole-read` | STAR    | Goal status |
|-------------------------------|---------------|------------------------------|---------|-------------|
| Mapping accuracy (≤100 bp)    | 93%           | 93%                          | ≈93%    | **MET**     |
| Canonical SJ recall           | 14/15         | 14/15                        | 12/15   | **MET (beats STAR)** |
| Non-canonical SJ recall       | 25/30         | 25/30                        | 24/30   | **MET (beats STAR)** |
| Non-canonical SJ precision    | 50% (30/60)   | **52%** (30/58)              | 100% (24/24) | **improved, still < STAR** |
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
The only unmet metric, now much smaller: mpmap 47% vs STAR 100% on the clean control. The residual
false calls are **low-support (median 1 read) ±1-3 bp positional/motif duplicates of TRUE
junctions** — 30/32 within 200 bp of a true site, 24/32 share both endpoints with a true junction
but carry a shifted wrong-motif label. Filter behavior: a distance-collapse (STAR
`outSJfilterDistToOtherSJmin` analogue, keep highest-support within W) lifts precision **47%→63%**
at W≥8 but costs recall **23→19**; overhang and unique-read filters do not separate true from false
(both mostly singletons, both high overhang). The residual gap is the **whole-read best-window**
architectural difference: STAR reports one splice per read cluster; mpmap's splice rescue reports
per-read positional/motif variants.

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
Choose per review: (a) **whole-read best-window** in splice rescue (the real lever, larger change:
compare the rescued spliced alignment to the read's best alternative and keep the whole-read
optimum), or (b) ship the portable **distance-collapse SJ filter** (`outSJfilterDistToOtherSJmin`
analogue) as an interim precision option (47%→63%, recall 23→19).

## Flag & feature inventory (verified against `src/subcommand/mpmap_main.cpp`, 2026-07-10)
All flags below are default-off; default mapping/splice output is byte-identical when unused.

- **MMP seeding (STAR-style front end)** — `mmp_seeding_implementation_plan.md`, `star_first_pass_plan.md`:
  `--mmp-seed` (splice-rescue MMP re-seed), `--mmp-primary` (MMP replaces the MEM pool),
  `--mmp-augment` (MMP appended to MEM pool), `--mmp-chain` (sequential MMP walk),
  `--mmp-strand-mode {native|rc|both}`; density/bound knobs `--mmp-min-prefix`, `--mmp-hit-max`,
  `--mmp-max-intron`, `--mmp-max-seeds`, `--mmp-start-lmax`, `--mmp-start-lmax-over-lread`,
  `--mmp-seed-per-read-max`; `--mmp-relax-accept N` (relaxed short-overhang acceptance, MMP candidates only).
- **Splice scoring** — `mmp_star_parity_plans.md` (Plan B), `star_first_pass_plan.md` (item 6):
  `--splice-motif-scores FILE` (per-motif donor/acceptor frequency table; **the relaxed-budget recall lever**),
  `--sjdb-score N` (bonus for annotated/graph-edge junctions; partial).
- **Junction output** — `star_first_pass_plan.md` (item 7):
  `--sj-out FILE` (graph-native SJ table: donor/acceptor `node:offset:strand`, motif, annotated flag,
  unique/multi read support, max overhang; donor coordinate fixed to the splice point),
  `--sj-reads FILE` (debug: per-junction supporting read names + chosen splice score, for read-level
  diffs against another aligner; drove the STEP 1 whole-read-best-window verdict).
- **Experimental precision (open work)** — `seed_pair_splice_generation_plan.md`:
  `--mmp-splice-pairs` (seed-pair-gated candidates + non-canonical partner constraints; does NOT yet recover precision).
- **Proposed but NOT shipped** (do not cite as current): `--mmp-extend` (built then removed — degraded mapping),
  `--mmp-both-strands`, `--sj-ref-path`, `--sj-min-overhang`, `--sj-min-reads`, `--sjdb-overhang-min`,
  `--novel-overhang-min`, `--splice-motif-preset`.

## Document map (all splice/SJ docs, 2026-07-10)
| Doc | Role / phase | Status | Substrate |
|-----|--------------|--------|-----------|
| **`mpmap_vs_star_sj_STATUS.md`** (this) | current goal + standing; the hub | authoritative | chr20 `refpath` |
| `star_vs_mpmap_sj_precision_diagnostic_plan.md` | precision diagnostic (the next step) | active plan, not yet run | chr20 `refpath` |
| `seed_pair_splice_generation_plan.md` | seed-pair precision approach | **REFUTED** | chr20 |
| `mmp_seeding_implementation_plan.md` | foundational MMP-seeding impl (M0–M7) | implemented | MHC (earlier) |
| `mmp_star_parity_plans.md` | follow-ups: A chaining / **B motif (recall win)** / C RC tail | implemented | MHC (earlier) |
| `star_first_pass_plan.md` | STAR first-pass items 2/3/6/7/5 (ships `--sj-out`) | implemented | MHC (earlier) |

**Substrate note:** the three implementation docs benchmarked on the MHC pangenome fixtures
(`panSC/tests/fixtures/mhc/sampleA.spliced`, 466-hap). Those numbers predate the move to chr20, which
was adopted because MHC paralogy confounded coordinate precision/recall. **chr20 `refpath` is now the
authoritative benchmark substrate.**
