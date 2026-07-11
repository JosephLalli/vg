# mpmap non-canonical splice discovery — minimal branch

> **Navigation:** Hub / entry point for all splice/SJ docs: [`mpmap_vs_star_sj_STATUS.md`](mpmap_vs_star_sj_STATUS.md).
> This doc records the minimal, upstreamable extraction of the de-novo splice-discovery result in
> `beat_star_splice_discovery_implementation.md`, and the re-measured numbers (with the
> `--max-motif-pairs` budget lever actually applied) that superseded that doc's headline table.

**Status (2026-07-11):** DONE. Branch `mpmap-noncanonical-splice-minimal`, two commits, built and
verified against the full `mmp-splice-seeding` branch on the standing chr20 `refpath` control.

## Why this branch exists
`mmp-splice-seeding` (this branch) carries the full exploration history: MMP seeding (M0-M7, later
shown not to be the lever), the `--trace-splice-search` diagnostic instrumentation (a separate
deliverable — see `mpmap-splice-search-trace`), several precision-filter experiments that didn't pan
out, and the de-novo discovery work. The minimal branch answers "what is the smallest diff that
reproduces the result?" — useful both as an upstream-facing patch and as a clean baseline for future
work.

## Base
`origin/master` at `6193310e` (upstream vg head at the time of extraction) — not `v1.75.1`. The
*build* used to verify the binaries was pinned to the v1.75.1 base (the toolchain-compatible build
point in this environment; see `BUILDING-LOCAL.md` for why master itself does not build here — it is
19.6k commits ahead and needs toolchain versions this environment doesn't have).

## Commit 1 — `327a4940a`, Lean: the necessary algorithm + measurement
`mpmap: non-canonical splice discovery via custom motif priors + --sj-out`

- `src/splicing.cpp`: `SpliceStats::init` treats per-motif frequencies summing to more than 1 as
  independent per-motif log-odds priors (warn instead of exit). This is the **only real algorithmic
  change**, ~6 lines — it is what allows many non-canonical motifs to be registered at realistic
  priors simultaneously without the shared-budget dilution that would otherwise sink each below the
  splice-acceptance threshold.
- `MultipathMapper::set_splice_motifs` — installs a caller-supplied motif table (mirrors the existing
  `set_intron_length_distribution`).
- Flags: `--splice-motif-scores FILE` (per-motif donor/acceptor frequency table), `--sj-out FILE`
  (graph-native junction table: donor/acceptor `node:offset:strand`, motif, annotated flag,
  unique/multi read support, overhang, plus reference-path coordinate columns), `--sj-min-unique N`
  (drop non-annotated junctions with fewer than N unique reads).
- New self-contained module `src/mpmap_sj.{hpp,cpp}` — the junction table, recorded from the
  splice-rescue path only (same scope limit as on `mmp-splice-seeding`; see the STATUS doc's flag
  inventory).
- **`--max-motif-pairs` is upstream**, already present in `origin/master` — this commit does not add
  it, only exercises it via a larger `--max-motif-pairs` value at call time.

## Commit 2 — `35ff1bda2`, optional high-accuracy: `--splice-denovo`
`mpmap: --splice-denovo high-accuracy whole-read best-window re-score`

- Whole-read M1/M2 infrastructure: `splice_eval_all` (disables the branch-and-bound early-out so all
  candidates are evaluated), `splice_whole_read` (collects gate-passers and re-ranks by a whole-read
  alignment across a donor-exon -> acceptor-exon graph), `whole_read_score`, `star_motif_bonus`.
- `--splice-denovo` implies `splice_eval_all` + `splice_whole_read`, relaxes the entry gate's motif
  penalty so rare non-canonical junctions can enter the candidate pool, and auto-raises
  `--max-motif-pairs` 200 -> 20000.
- Knobs: `--splice-whole-read-topk` (default 8 — the runtime lever), `--splice-whole-read-context`,
  `--splice-whole-read-motif-weight`.
- **Default off -> byte-identical to the Lean path when unused** (verified).

## What was cut (unnecessary for this goal, verified inert on this substrate)
| Component | Size | Why cut |
|---|---|---|
| MMP seeding (`src/mpmap_mmp.*`, all `--mmp-*` flags) | ~518 lines | Seeding was proven not the lever — MEM and MMP give identical splice-discovery results (STATUS doc, "MEM vs MMP" row) |
| Trace instrumentation (`src/mpmap_trace.*`) | ~771 lines | A separate diagnostic deliverable; lives on branch `mpmap-splice-search-trace`, not needed to reproduce the discovery result |
| Paralog/experimental filters: `--sj-slide`, `--sj-anchor-multimap-max`, `--sjdb-score`, `--mmp-splice-pairs` | — | All default-off; none recovered precision beyond what the budget lever + `--sj-min-unique` already deliver (see `beat_star_splice_discovery_implementation.md`, "Empirical confirmation" and "Experimental precision" items) |

## Results (chr20 `refpath`, genome-unique control, `--sj-min-unique 3`)
Re-measured with the `--max-motif-pairs` budget lever actually applied to the `refpath` control —
this table supersedes the STATUS doc's earlier "25/30 @ 52%" headline, which never applied the
budget lever on this control (see that doc's "Current standing" section for the correction note).

| Recipe | Canonical recall | Canonical precision | Non-canonical recall | Non-canonical precision |
|---|---|---|---|---|
| Lean (`--splice-motif-scores` + `--max-motif-pairs 20000` + `--sj-min-unique 3`) | 15/15 | 100% | 30/30 | 97% |
| + `--splice-denovo` | 15/15 | 100% | 30/30 | **100%** |
| STAR pass-1 | 12/15 | 57% | 24/30 | 100% |

mpmap beats STAR on canonical recall and canonical precision, ties/exceeds on non-canonical recall,
and with `--splice-denovo` ties STAR's non-canonical precision exactly. The budget lever does
roughly 94-97% of the work; `--splice-denovo` closes the remaining ~3 points of precision (and the
last unit of recall between the two mpmap recipes' rounding).

This corrects the earlier characterization of `--splice-denovo`'s whole-read gate as "inert" (see
`graph_denovo_splice_discovery_plan.md`'s superseded-header and
`beat_star_splice_discovery_implementation.md`'s "BREAKTHROUGH" section, both written from an ablation
that did not use the curated/scored motif regime this table uses). In the curated-motif regime the
whole-read gate is load-bearing for the last precision gap and is not separable from the M1/M2
infrastructure it depends on (`--splice-denovo` implies `--splice-eval-all` and
`--splice-whole-read`). The "inert" finding stands only for its original regime: all-256 flat motifs
with a manually-raised budget.

## Runtime ladder (180k splice-heavy reads, `refpath`, `-t 16`, budget fixed at 20000 for both mpmap rows)
| Build | Throughput | vs stock |
|---|---|---|
| Unmodified/stock `vg mpmap` | ~7,930 reads/s | baseline |
| Lean (custom motifs + budget) | ~7,087 reads/s | **+12%** over stock |
| `--splice-denovo` | ~3,371 reads/s | 2.0x vs Lean, 2.35x vs stock |

**Mechanism of the `--splice-denovo` cost:** for every splice-candidate read it disables the
branch-and-bound early-out (evaluates all candidates instead of stopping at the first acceptable one)
and re-aligns the *whole read* across a donor-exon -> acceptor-exon graph for each of the top-K
(default 8, `--splice-whole-read-topk`) candidates. This cost is confined to the de-novo
splice-rescue path — reads that never trigger splice rescue pay nothing. On an annotated pangenome,
where most spliced reads traverse already-annotated edges and never enter rescue (see the STATUS
doc's `--sj-out` scope-limit note), the real-world overhead is far smaller than this splice-heavy
synthetic figure. Both the cost and the benefit of `--splice-denovo` are concentrated on the
de-novo-discovery workload.

**Recommendation:** position `--splice-denovo` as an opt-in "high-accuracy de-novo mode" (default
off, which it already is). The Lean recipe is the recommended default path — it beats STAR at only
+12% over stock runtime. `--splice-whole-read-topk` is the available runtime lever if
`--splice-denovo`'s 2x cost needs to be cut; lowering it trades re-score thoroughness for speed and
has not itself been swept.

## Verification
Both binaries (Lean-only and Lean+`--splice-denovo`) link cleanly against the v1.75.1 build base.
The Lean binary reproduces 30/30 @ 97% non-canonical and the `--splice-denovo` binary reproduces
30/30 @ 100%, each producing a byte-identical junction set to the full `mmp-splice-seeding` binary
for the same recipe and inputs.

## Document map pointer
See `mpmap_vs_star_sj_STATUS.md`'s Document Map for how this fits alongside the exploration-history
docs (`graph_denovo_splice_discovery_plan.md`, `beat_star_splice_discovery_implementation.md`, etc.).
This doc is the destination for "what should actually ship" questions; the others are the record of
how that answer was found.
