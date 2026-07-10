# mpmap vs STAR on splice-junction detection — STATUS (authoritative)

Single source of truth for the current goal and standing. A **Document Map** of every related doc,
and a **verified flag inventory**, are at the bottom. Last updated 2026-07-10.

## Current goal
Make **vg mpmap perform as well as or better than STAR on BOTH precision and recall** of
splice-junction detection — **canonical and non-canonical** — on a **reference-haplotype-only
chr20 graph** **with introduced non-canonical junctions**. Either the MEM or MMP seeding pathway
is acceptable (seeding has been shown not to be the lever).

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
- **Reference:** STAR pass-1 (`/usr/bin/STAR` 2.7.11b), de novo.
- **Harness:** `$CLAUDE_JOB_DIR/tmp/chr20bench` — `score_pc.py`, `truth_pc.tsv`, `reads_pc*.fq`,
  `design_pc.py`, `motif_curated.txt`, `sj_lin_*.tsv`. (Job scratch is ephemeral; the numbers below
  are the durable record.)

## Current standing (2026-07-10, measured on `refpath`)
Default config = MEM + relaxed-budget curated motifs (the recall-win config). `+splice-pairs` =
adding experimental `--mmp-splice-pairs`.
| Metric                        | mpmap (default) | mpmap (+splice-pairs) | STAR    | Goal status |
|-------------------------------|-----------------|-----------------------|---------|-------------|
| Mapping accuracy (≤100 bp)    | 93%             | 93%                   | ≈93%    | **MET**     |
| Canonical SJ recall           | 12/15           | 12/15                 | 12/15   | **MET (tie)** |
| Non-canonical SJ recall       | **14/30**       | 10/30                 | 10/30   | **MET (exceeds/tie)** |
| Non-canonical SJ precision    | **2%** (20/1081) | **4%** (11/307)      | ~91%    | **NOT MET** |
| MEM vs MMP (any metric)       | identical       | identical             | —       | seeding is not a lever |

`--mmp-splice-pairs` trades recall (14→10/30) for a negligible precision gain (2→4%) by cutting
total junctions (1272→481) — the same pattern seen on the diagnostic graph; it is not a fix.

## What is done
- **Relaxed splice-motif frequency budget** (committed `9baee7e`): treat per-motif frequencies as
  independent log-odds priors (sum may exceed 1), so a motif file keeps GT-AG dominant while
  admitting non-canonical motifs at a flat prior. Non-canonical recall **0/30 → 14/30 on `refpath`**
  (17/30 on the diagnostic graph), both exceeding STAR's 10/30. The **recall half of the goal
  (canonical + non-canonical) is MET.**
- **`--sj-out max_overhang` populated** (was a hardcoded 0).
- **`--mmp-splice-pairs`** experimental precision levers (partner-source restriction, stitch-
  mismatch cap, partner-seed uniqueness, partner-alignment-mismatch cap). All default-off
  byte-identical; **none recover precision.**

## What is open — non-canonical PRECISION
The only unmet metric. Four distinct levers plus overhang/read-count/multi-read filters all
failed. Root-cause characterization (2026-07-10):
1. **Benchmark confounder:** the designer exons are random CHM13 fragments containing repeats;
   **73% of mpmap's false junctions are genome-wide, unique-read splices** from those repeats
   (reads' Alu/LINE halves match elsewhere). A repeat-free control is needed to know the true gap.
2. **Architectural difference:** STAR scores the whole read to its single best genomic window and
   reports junctions only from there; mpmap's splice rescue anchors one exon and accepts a
   locally-optimal soft-clip-tail partner. This is the leading hypothesis for the real gap.

## Next step
`star_vs_mpmap_sj_precision_diagnostic_plan.md`: (0) rebuild a genome-unique control to remove the
repeat confounder, then (1) a read-level head-to-head — for each mpmap false junction, classify how
STAR suppressed the same reads (no-splice / spliced-true / multimapping / filtered). The resulting
histogram decides whether the fix is a benchmark artifact, a portable `outSJfilter`, multimapping-
aware reporting, or the whole-read best-window re-architecture. Only then do we implement.

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
  unique/multi read support, max overhang).
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
