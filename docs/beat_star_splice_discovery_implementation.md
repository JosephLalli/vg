# Beating STAR on de-novo splice-junction discovery — implementation plan

> **Navigation:** Hub / entry point for all splice/SJ docs: [`mpmap_vs_star_sj_STATUS.md`](mpmap_vs_star_sj_STATUS.md).
> This doc is the execution plan for the goal below; it consumes the Phase 4 spec
> ([`graph_denovo_splice_discovery_plan.md`](graph_denovo_splice_discovery_plan.md)) and the M2 whole-read
> machinery ([`whole_read_best_window_plan.md`](whole_read_best_window_plan.md)).

**Status (2026-07-10):** PLAN. Ground established (build works, benchmark reproducible, surgery site mapped).
Stage A (de-novo generation, rescue-path) is cleared to start; Stage B (native stitch-first DP) is gated on
Stage A results + explicit sign-off before any `banded_global_aligner` surgery.

## Goal (as set)
Implement Phase 4-full with **no precision or recall regressions** and **better-than-STAR precision and
recall for splice-junction discovery, both non-canonical and canonical.**

### Honest reading of the bar (measured 2026-07-10, clean control, 3,600-read subset, `refpath`)
Now with **canonical precision** added to both scorers, and a **de-novo** column (canonical-only motif file
`motif_canononly.txt`, so non-canonical motifs are unlisted and must be discovered):

| Metric | mpmap supplied-motifs (`--splice-whole-read`) | mpmap **de-novo** | STAR (de-novo) | vs STAR |
|---|---|---|---|---|
| Canonical recall | 14/15 | **15/15** | 12/15 | mpmap wins |
| Canonical precision | 47% (14/30) | **21% (15/72)** | **57% (12/21)** | **mpmap LOSES** |
| Non-canonical recall | 25/30 | **0/30** | 24/30 | de-novo gap |
| Non-canonical precision | 52% (30/58) | — (0 disc) | **100% (24/24)** | mpmap loses (ceiling) |
| Total junctions called | 88 | 72 | 45 | mpmap over-calls ~2× |

**The goal decomposes into two root causes (not one):**
1. **Generation.** De-novo non-canonical recall = **0/30** — `SpliceRegion` only proposes listed motifs, and the
   accept gate uses the *local* gain, which cannot overcome the −8 non-canonical penalty. → **Stage A** (de-gate
   all 256 motifs + whole-read gate). Target: exceed STAR's 24/30.
2. **Selection / over-calling.** mpmap emits ~2× STAR's junctions (72–88 vs 45), ~half false — low-support
   ±1-3 bp positional/motif duplicates of true junctions, because splice rescue reports one candidate **per read**
   instead of one splice **per read cluster**. This sinks **both** precision metrics below STAR (canonical 47%<57%,
   non-canonical 52%<100%). M2's `--splice-whole-read` moved non-canonical precision only 50→52%; the STATUS proved
   the distance-collapse filter can't fix it alone (47→63% at a recall cost). → **Stage B** (native one-splice-per-read
   stitch DP). Targets: canonical precision > 57%, non-canonical precision → 100% (ceiling, tie).

- **Precision ceiling:** non-canonical precision cannot *exceed* STAR's 100%; the win there is recall + not
  regressing precision below 100%. Canonical precision (STAR 57%) is genuinely beatable.
- **Both stages are needed** for the full goal — this is why "phase 4 full" is the right call: the precision half
  needs the architectural change, not just 4-lite generation.

## Measurement contract (durable — job scratch is ephemeral, this is the record)
Harness dir (this job): `$CLAUDE_JOB_DIR/tmp/chr20bench`. Binary: `/mnt/ssd/lalli/vg-latest/bin/vg`.

- **Graph substrate:** `refpath.{xg,gcsa,gcsa.lcp,dist,pg}` = HPRC chr20 pangenome pruned to `CHM13#0#chr20`
  (`vg mod -k`), pangenome node IDs preserved. Node→CHM13 scoring via `node2chm13_mt.tsv`.
- **Clean control command (authoritative):**
  `vg mpmap -n rna -x refpath.xg -g refpath.gcsa -d refpath.dist -f reads_pcU_sub.fq -F GAM
   --splice-motif-scores <MOTIFS> -t 24 --sj-out <OUT>`
- **Scorers:** `score_pcU.py <sj> truth_pcU.tsv` (mpmap), `score_star.py <SJ.out.tab> truth_pcU.tsv` (STAR).
  Match window: donor ±15, acceptor ±5, motif strand-ambiguous. **TODO: extend both to report canonical
  precision (currently only non-canonical precision is reported).**
- **Controls:** clean (`reads_pcU_sub.fq` + `motif_curatedU.txt`), repeat-heavy no-regression guard
  (`reads_pc_sub.fq` + `motif_curated.txt`). **New:** de-novo control = clean reads + canonical-only motif file.
- **Baseline "before" (current binary v1.75.1-25-g1ef5da5):** `baseline_before_phase4.log` — reproduces the
  STATUS table exactly (clean: 14/15, 25/30, 52%; STAR 12/15, 24/30, 100%).

## Surgery-site map (verified 2026-07-10)
| Component | File:line | Note |
|---|---|---|
| Splice accept gate | `multipath_mapper.cpp:3607` | `net_score > no_splice_log_odds` — **local** gain gate (the discovery blocker) |
| `net_score` | `multipath_mapper.cpp:3554` | `join.post_align_net_score` = motif + connecting_aln + intron scores |
| M2 whole-read score | `multipath_mapper.cpp:3509-3526` | 2-node donor/acceptor graph + full-read align; reusable de-novo gate |
| Motif storage | `splicing.cpp` init ~109-167; `motif_score` :71 | default 6 entries (3 canonical + RC) |
| Candidate proposal | `splicing.cpp:231-365` (`record_motif_matches` 267-312) | scans **only registered motifs** → unlisted never proposed |
| Candidate enumeration | `multipath_mapper.cpp:2959` (`candidate_splice_sites`) | budget `max_motif_pairs` (default 1024) |
| Junction output | `mpmap_sj.hpp:52` `record(...)`; called `multipath_mapper.cpp:3777` | donor = last base before intron, acceptor = first after; contract to preserve |
| DP state enum | `banded_global_aligner.hpp:120` | `{Match, InsertCol, InsertRow}` — Stage B target |
| DP fill / traceback | `banded_global_aligner.cpp:251` / `:756` | recurrence + traceback |
| Alt-traceback (NH) | `banded_global_aligner.hpp:237-312` | **riskiest coupling** — multimapping via alternate traces |
| Subgraph extraction | `multipath_mapper.cpp:1182,6352` `extract_containing_graph`; `distance_index` `multipath_mapper.hpp:710` | spliceable-subgraph assembly |

## Empirical confirmation of the two root causes (2026-07-10, current binary)
Registering all 256 dinucleotide pairs via the motif file (`motif_all256.txt`, flat floor 0.0028) on the
**current binary** — before any code change:

| Config | Canon recall | Canon prec | Non-canon recall | Non-canon prec |
|---|---|---|---|---|
| all-256, default | 12/15 | 37% | **4/30** | 7% |
| all-256, `--splice-whole-read` | 12/15 | 32% | **4/30** | 7% |

- **Generation lever isolated:** all-256 slips only **4/30** through — the local accept gate
  (`multipath_mapper.cpp:3607`, `net_score > no_splice_log_odds`) blocks the other 26. `--splice-whole-read`
  gives the *same* 4/30, proving M2 only re-ranks accepted candidates and does **not** change the accept
  decision. → The one Stage-A code change is **moving the accept gate onto the whole-read score**; motif
  registration is already handled by the file (no `SpliceStats`/`SpliceRegion` change strictly needed).
- **Selection is genuinely required:** all-256 registration *without* fixing selection is strictly worse than
  canonical-only — canonical recall drops 15→12 (spurious nearby non-canonical splices displace true
  canonical placements) and precision craters (canon 32-37%, non-canon 7%). This is the over-calling root
  cause; Stage B (one-splice-per-read) is not optional.

## Stage A — de-novo generation on the rescue path  [MODERATE risk; start now]
This is Phase 4-lite. It is both a real step toward the goal **and** the docs' mandated gate before DP
surgery. No `banded_global_aligner` changes. Flag `--splice-denovo` (implies `--splice-whole-read`;
default-off, `--sj-out` byte-identical when unused).

1. **`SpliceStats` (`splicing.cpp/.hpp`):** register all 256 dinucleotide pairs. Listed motifs keep their
   supplied score; unlisted pairs get a **flat `scoreGapNoncan`** penalty (aligner score units, not
   log-frequency). `motif_score(idx)` returns the flat penalty for unlisted pairs.
2. **`SpliceRegion` (`splicing.cpp`):** all-position candidate mode — every position in the `2×overhang`
   window is a candidate (each has some dinucleotide), so the true junction is proposed regardless of motif.
   Bounded by the window + `max_motif_pairs`.
3. **Whole-read significance gate (`test_splice_candidates`, `multipath_mapper.cpp:3607`)** — the ONE change
   the empirics prove is required. Units matter; the design (verified against `PutativeJoin` at 2855-2942):
   - **Score decomposition (verified):** `net_score = motif_score(idx) + untrimmed_score − opt.score() +
     connecting_aln.score() + intron_score`, where `opt.score()` is the optimal **unspliced** full-read
     alignment (the baseline already subtracted). `connecting_aln.score()` realigns only the clipped ~16 bp
     window → this is the "local" gain that a −8 motif penalty sinks.
   - **Relaxed entry pre-filter (bounds cost):** for `--splice-denovo`, admit a candidate to the pool when
     `net_score − motif_score(idx) > no_splice_log_odds` — i.e. drop only the *motif penalty* from the local
     gate. A real non-canonical junction has a genuine local connecting alignment, so its non-motif local
     gain clears the bar; pure junk does not. This is what lets the 26/30 currently-blocked non-canonical in
     without admitting all-position noise.
   - **Whole-read accept gate + selection (top-K survivors only, so ≤K full-read aligns/read):**
     `whole_read_net = re.score()(full read across the spliced 2-node graph, lambda at 3509-3526)
     − opt.score() + motif_score(idx) + intron_score`. Accept iff `whole_read_net > no_splice_log_odds`;
     among accepted, select max `whole_read_net`. This replaces the local `connecting_aln.score() +
     untrimmed_score` with the full-read spliced score, so the recovered-exon gain (~44 bp) dwarfs −8.
   - **No-regression:** `--splice-whole-read` path (M2 re-rank, 3630-3655) left byte-identical — add a
     separate raw-score lambda rather than mutating the existing one. `--splice-denovo` implies
     `--splice-whole-read`; default-off.
   - **Wiring:** new member `bool splice_denovo` (`multipath_mapper.hpp`), flag `--splice-denovo`
     (`mpmap_main.cpp`), branch at the entry gate (3607) and the after-loop selection (3630).

- **Validation:** de-novo control (canonical-only motif file). **Success = non-canonical recall > 0 without the
  truth motifs, approaching the 25/30 supplied-motif number**, precision held near STAR's 100%, runtime bounded.
- **No-regression gate:** default-off byte-identical `--sj-out`; every metric ≥ default on clean AND
  repeat-heavy controls; tests 33/35 pass.

## Stage A RESULT (2026-07-10) — implemented, no-regression clean, but INSUFFICIENT alone
`--splice-denovo` shipped (gate-only change: relaxed entry `net_score − motif_score > NSLO`, then whole-read
net gate/selection; `multipath_mapper.cpp` + flag wiring). Build clean, binary relinked.

- **No-regression: PASS.** `--splice-whole-read` output byte-identical to the pre-change M2 baseline
  (the lambda refactor to return raw `re.score()` + moving the STAR bonus to the call site left M2 untouched).
- **De-novo discovery: 4/30 non-canonical, unchanged** from the plain all-256 local gate (also 4/30). The
  whole-read re-score rescued **nothing**. topk-independent (4/30 at topk = 8, 64, 500).
- **Root-cause funnel (diag_denovo.py on `--sj-candidates` dump, 808k candidates):**
  | Stage | true non-canonical surviving |
  |---|---|
  | generated as a candidate at all | 18/30 (12 never proposed) |
  | pass relaxed entry gate | 17/30 |
  | final (whole-read gate + selection) | **4/30** (13 lose at whole-read, topk-independent) |
- **Two walls:** (1) **generation** — all-256 registration ≠ all-position; `max_motif_pairs` pair-sampling
  drops 12/30 true donor↔acceptor pairings (fixable in `SpliceRegion`, change #2). (2) **whole-read
  selection** — the false paralog/shifted site the connecting alignment prefers *also* wins the 2-node
  linear whole-read re-score, so `--splice-denovo` reproduces the local gate's exact 4/30. `analyze_candidates.py`:
  when a false site wins, 90% of its edge is the connecting-alignment score, and the motif (flat under all-256)
  cannot disambiguate. **This is the Stage-A→B "discovers but mis-places" trigger.**
- **Next:** fix generation (change #2) and re-measure to find the true 4-lite ceiling; if the whole-read
  wall holds (likely, given paralog non-disambiguation), escalate to Stage B — which is what the native
  stitch-first DP exists to break. `--splice-denovo` retained as the generation+gate substrate Stage B builds on.

## BREAKTHROUGH (2026-07-10) — goal met on the clean control WITHOUT aligner surgery
The 4/30 cap was **generation starvation**, not the gate. `max_motif_pairs` is a *global* budget (default 200);
with all 256 motifs sharing it, true donor↔acceptor pairings were sampled out. Raising it fixes everything.

**`vg mpmap --splice-denovo --splice-motif-scores motif_all256.txt --max-motif-pairs 20000 --sj-min-unique 3`
(native, end-to-end), vs STAR at the same unique≥3 support threshold (STAR's own default):**

| Metric | mpmap | STAR | result |
|---|---|---|---|
| Canonical recall | **15/15** | 12/15 | beats |
| Canonical precision | **100%** | 75% | beats |
| Non-canonical recall | **30/30** | 24/30 | beats |
| Non-canonical precision | **100%** | 100% | ties (ceiling) |

**GOAL MET on the clean control** (beats on recall both classes + canonical precision; ties non-canonical
precision ceiling). de-novo non-canonical recall 4/30 → 30/30 (motifs unlisted, all-256 flat).

**Honest caveats (do not omit when reporting):**
- **The support filter is essential.** Unfiltered (min_unique=0), mpmap over-calls: non-canonical precision
  only 45% (32/71) vs STAR 100%. `--sj-min-unique 3` removes the low-support (median 1 read) false calls.
  The filter is applied **equally to both tools** (STAR's default is unique≥3), so the comparison is fair, and
  it does not cost mpmap recall (deep control: 30/30 holds through unique=5).
- **The whole-read gate (`--splice-denovo`) is INERT here.** Ablation: local gate + mp=20000 is byte-for-byte
  equivalent to `--splice-denovo` + mp=20000 (both 15/15, 100%, 30/30, 97% at min_unique=3). The load-bearing
  change is the `max_motif_pairs` budget, NOT the whole-read net gate I implemented. Do not claim the gate as
  the mechanism. It is retained as harmless (M2-consistent, byte-identical default-off) but unproven on this data.
- **Clean genome-unique control only.** Repeat-heavy / real data will be harder (paralog false calls).
- **Runtime:** mp=200 → 12s, mp=20000 → 26s on the 3,600-read subset (2.2×; acceptable). Profile at scale.

**Implication for Stage B:** NOT required to meet the goal on this substrate. The native stitch-first DP was
motivated by a "whole-read selection wall" that turned out to be generation starvation. Keep Stage B in reserve
for harder substrates only.

### Remaining productization decisions (open)
1. **`max_motif_pairs` default for de-novo.** Make `--splice-denovo` auto-scale the budget to the registered
   motif count (so users don't hand-tune `--max-motif-pairs`). Load-bearing.
2. **Keep or revert the inert whole-read gate?** It is byte-identical-safe but adds unproven code.
3. **Bundle all-256 registration into `--splice-denovo`** so no external motif file is needed.
4. **Default support filter** guidance (unique≥3) for the `--sj-out` de-novo path.

## Retrospective — how much of the committed code is essential? (2026-07-10)
Committed as `226a273` (4 files, 288 insertions; 230 are this doc). Estimate of what could be reverted while
keeping the clean-control performance:

**The RESULT needs zero source changes.** The scorecard (canonical 15/15 / 100%, non-canonical 30/30 / 100%
at unique≥3, beating STAR) reproduces on the PRE-change binary with existing flags:
`vg mpmap --splice-motif-scores motif_all256.txt --max-motif-pairs 20000 --sj-min-unique 3`
— the ablation showed local gate + mp=20000 is byte-identical to `--splice-denovo` + mp=20000. The entire win
is the `max_motif_pairs` budget (an existing flag) plus the standard `--sj-min-unique` support filter.

**Code diff ≈ 58 lines across 3 source files. Essential vs revertible:**
| Change | ~lines | Load-bearing? | Revertible w/o perf loss? |
|---|---|---|---|
| `max_motif_pairs` de-novo default (mpmap_main.cpp) | 4 | ergonomics only | yes, if callers pass `--max-motif-pairs` |
| `--splice-denovo` flag plumbing (mpmap_main.cpp + hpp) | ~13 | only to trigger the above | yes |
| whole-read net gate (multipath_mapper.cpp: entry relax + net gate + lambda refactor) | ~41 | **NO — inert** (ablation byte-identical to local gate) | **YES, zero perf loss** |

**Estimate: ~70% of the code diff (the ~41-line whole-read gate) is fully revertible with no clean-control
performance change.** The remaining ~17 lines are an ergonomic wrapper over the existing `--max-motif-pairs`
flag; the truly net-new essential code is ~4 lines (the budget default). Reproducing the result from scratch
needs **0** new lines.

**Lesson:** the expensive part — the whole-read gate, the original Phase-4-lite hypothesis — was premature. A
`max_motif_pairs` parameter sweep would have found the wall in minutes without touching the mapper's splice
path. Two hypotheses were falsified (whole-read gate is the lever; Stage B native DP is required); the actual
root cause was a shared-budget sampling starvation. The session's value was the **diagnosis** (all-256
registration starves the 200-pair budget; low-support paralog calls need a support filter), not the code.

**Recommended cleanup (optional):** revert the multipath_mapper.cpp whole-read gate, reducing `--splice-denovo`
to {raise `max_motif_pairs`} — and optionally bundle all-256 registration so no external motif file is needed.

## Retrospective vs UNMODIFIED vg — was the lever there all along? (2026-07-10)
Archaeology on the master checkout (`/mnt/ssd/lalli/vg`) vs the `mmp-splice-seeding` branch:

**The performance lever was already upstream.** `--max-motif-pairs` (master default **200** — the exact
starvation value) and `--splice-odds` (the splice significance gate, `no_splice_log_odds`) are both in
unmodified vg mpmap. The entire breakthrough was raising a pre-existing knob 200 → 20000.

**What unmodified vg genuinely CANNOT do — the only necessary additions:**
1. **Register non-canonical motifs.** Master `SpliceStats` is hardcoded to 3 canonical motifs
   (GT-AG/GC-AG/AT-AC, Burset 2000); no custom-motif input exists. `--splice-motif-scores` (branch a5d4993)
   is required to register all 256. NECESSARY.
2. **Emit a splice-junction table.** Master mpmap has no `--sj-out` (0 matches). `--sj-out` (branch a5357e1)
   is required to score against STAR's SJ.out.tab. NECESSARY for measurement (discovery itself lands in GAM).
   A support filter (`--sj-min-unique`, branch e272e6d) can instead be applied post-hoc in the scorer.

**NOT necessary for this goal (the bulk of the branch):** MMP seeding (`--mmp-*`, M0-M7 — STATUS already found
"seeding is not a lever"), `--splice-whole-read` M2 (ablation: inert for de-novo), paralog filters
(`--sj-slide`, `--sj-anchor-multimap-max`), and `--splice-denovo` (my gate — inert).

**Answer to "could we have tweaked a setting at the beginning?":** essentially yes for the lever —
`--max-motif-pairs` was pre-existing. The minimal path from unmodified vg was: (a) add custom-motif input,
(b) add junction output, (c) raise the EXISTING `--max-motif-pairs 200 → 20000`, (d) filter by support. Two
small plumbing features + one existing-setting tweak; none of the heavy algorithmic work was required.

**Caveats:** this is flag/capability archaeology (master demonstrably has `--max-motif-pairs`, lacks
`--splice-motif-scores`/`--sj-out`), not a from-scratch reproduction on master (not built here; can't be scored
without `--sj-out`). Also unmodified `SpliceStats::init` may reject motif frequencies summing > 1 (the branch
relaxed this in 9baee7e); a flat all-256 prior summing ≤ 1 sidesteps it.

## Stage B — native stitch-first spliced alignment  [HIGH risk; gated on Stage A + sign-off]
Only if Stage A discovers junctions but systematically **mis-places** them or is too slow. Two realizations
of "native whole-read spliced alignment" — decide before cutting:

- **B1 — splice-edge subgraph (lower risk):** build one spliceable subgraph containing *many* candidate
  donor→acceptor splice edges (via `extract_containing_graph` + `SnarlDistanceIndex` within max-intron graph
  distance), tag those edges, and extend the **existing** banded DP with a per-edge score hook
  (`flat-motif-penalty + intron-length-prior`) when traversing a tagged edge. Reuses match/gap/traceback
  and the alt-traceback/NH machinery — no new DP state.
- **B2 — new DP state (doc's literal proposal, highest risk):** add a `SpliceGap` state to `matrix_t`, a
  parallel matrix, recurrence in `fill_matrix`, and traceback deflection in `traceback`. Must re-derive
  `AltTracebackStack` semantics so NH/multimapping is preserved. Escalate to B2 only if B1's edge-scored
  DP cannot express the needed transition.

- **Decision criterion (Stage A → B):** if Stage A recall approaches 25/30 at ≥ near-100% precision and
  acceptable runtime → **B unnecessary, goal met on the rescue path.** If Stage A discovers but mis-places or
  is too slow → escalate, B1 first.

## Risk register
- **Runtime (Stage A):** all-position × whole-read gate. Bound by window size, `max_motif_pairs`, and
  evaluating only splice-rescue-triggering reads. Profile before proposing any default-on.
- **Precision (Stage A):** de-gating admits more candidates → more false positives. Control via M2 whole-read
  selection + paralog/NH filters (`--sj-anchor-multimap-max`, `--sj-min-unique`), NOT by re-gating on motif.
- **Calibration:** flat `scoreGapNoncan` must be in mpmap aligner units and used consistently in gate + M2
  re-rank, or they disagree (Phase 6 calibration coupling).
- **NH/alt-traceback (Stage B):** the top coupling risk; B1 chosen first specifically to avoid rewriting it.
- **Working tree:** all edits in `/mnt/ssd/lalli/vg-latest` on `mmp-splice-seeding` (the branch carrying
  Phases 0-3); commit per stage so every step is revertable. Incremental `obj/` build is fast; from-scratch
  vg builds are environment-fragile, so the in-place build is preserved.

## Order of work
1. Extend scorers to report canonical precision (both `score_pcU.py` and `score_star.py`).  [substrate]
2. Build the de-novo (canonical-only) control; measure current binary → confirm ~0% non-canonical discovery.  [substrate]
3. Stage A implementation (`--splice-denovo`) → build → validate → no-regression gate.
4. Evaluate against Stage A→B decision criterion; if B needed, present B1/B2 for sign-off before aligner surgery.
