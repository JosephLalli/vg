# mpmap vs STAR on splice-junction detection — STATUS (authoritative)

Single source of truth for the current goal and standing. Sub-plans are linked at the bottom.
Last updated 2026-07-10.

## Current goal
Make **vg mpmap perform as well as or better than STAR on BOTH precision and recall** of
splice-junction detection — **canonical and non-canonical** — on a **linear graph genome**
(CHM13 chr20) **with introduced non-canonical junctions**. Either the MEM or MMP seeding pathway
is acceptable (seeding has been shown not to be the lever).

## Benchmark (the standing measurement)
- **Graph:** linear CHM13 chr20 (`vg construct` from the reference; no variation), so the
  comparison is head-to-head with STAR on the same linear genome.
- **Positive control:** 15 canonical (GT-AG) + 30 non-canonical junctions with *verified, diverse*
  non-canonical motifs, 150 bp exon anchors, ~800 bp introns, deep tiled reads (400/junction,
  1% error). Scored **motif-verified** against exact reference coordinates.
- **Reference:** STAR pass-1 (`/usr/bin/STAR` 2.7.11b), de novo.
- **Harness:** `$CLAUDE_JOB_DIR/tmp/chr20bench` — `score_pc.py`, `truth_pc.tsv`, `reads_pc*.fq`,
  `design_pc.py`, `motif_curated.txt`, `sj_lin_*.tsv`. (Job scratch is ephemeral; the numbers below
  are the durable record.)

## Current standing (2026-07-10)
| Metric                        | mpmap        | STAR    | Goal status |
|-------------------------------|--------------|---------|-------------|
| Mapping accuracy (≤100 bp)    | 93%          | ≈93%    | **MET**     |
| Canonical SJ recall           | 12–14/15     | 12/15   | **MET**     |
| Non-canonical SJ recall       | **17/30**    | 10/30   | **MET (exceeds)** |
| Non-canonical SJ precision    | **3%**       | ~91%    | **NOT MET** |
| MEM vs MMP (any metric)       | identical    | —       | seeding is not a lever |

## What is done
- **Relaxed splice-motif frequency budget** (committed `9baee7e`): treat per-motif frequencies as
  independent log-odds priors (sum may exceed 1), so a motif file keeps GT-AG dominant while
  admitting non-canonical motifs at a flat prior. Non-canonical recall **0/30 → 17/30**. The
  **recall half of the goal (canonical + non-canonical) is MET.**
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

## Sub-documents
- `star_vs_mpmap_sj_precision_diagnostic_plan.md` — the precision diagnostic (the active next step).
- `seed_pair_splice_generation_plan.md` — seed-pair candidate generation approach + its refuted validation.
- `mmp_star_parity_plans.md`, `star_first_pass_plan.md` — earlier MMP / STAR first-pass parity work
  (recall + mapping-rate phase; historical context).
