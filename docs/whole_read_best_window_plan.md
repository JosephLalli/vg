# Plan: whole-read best-window splice placement for vg mpmap (graph-native STAR adaptation)

> **Doc status (2026-07-10):** PROPOSED — planning for the residual non-canonical precision gap.
> Not yet implemented. Depends on: `mpmap_vs_star_sj_STATUS.md` (standing), the donor-coordinate fix
> (`8b6115c`), and the STEP 1 read-level verdict (whole-read best-window). Awaiting review before code.

## Goal & success criteria (hard constraints, per user 2026-07-10)
Adapt STAR's whole-read best-window splice placement to **graph** genomes and see whether it improves
mapping, junction discovery, and **non-canonical** junction discovery.

- Metrics tracked: (1) mapping accuracy ≤100 bp, (2) canonical SJ recall, (3) non-canonical SJ recall,
  (4) non-canonical SJ precision.
- **NO REGRESSION** in precision **or** recall vs the current vg mpmap (baseline) — hard gate.
- Ideally **meet or beat STAR on both** precision and recall.
- If a trade-off is forced, prefer **precision** over recall — but still never regress either vs baseline.
- **Default-off byte-identical**: the change is flag-gated; improvement is measured with the flag on,
  and the no-flag path must be unchanged (protects all existing users from regression by construction).

## Baseline to beat (clean chr20 control, current binary incl. donor fix; motif-strand-correct scorer)
| Metric | mpmap baseline | STAR |
|--------|----------------|------|
| Mapping accuracy ≤100 bp | ~93% | ~93% |
| Canonical SJ recall | 14/15 | 12/15 |
| Non-canonical SJ recall | 25/30 | 24/30 |
| Non-canonical SJ precision | 50% (30/60) | 100% (24/24) |

Residual false non-canonical = 30 junctions = **~19 low-support singleton jitter** (individual reads'
splices land ±1-3 bp off) **+ 5 high-support (49-69 reads) systematic ±1-2 bp donor mis-placements**
(the dominant placement for a hard junction is 1-2 bp wrong) + a few at support 2-4. Precision, not
recall, is the gap (recall already beats STAR). STAR reaches 100% because whole-read alignment with
150 bp exact-match anchors pins the splice consistently, and it emits motif code 0 (no strand/motif
commitment).

## Architecture: where mpmap places the splice (code refs)
`MultipathMapper::test_splice_candidates` (`src/multipath_mapper.cpp:2831`):
1. `trimmed_end(opt, max_splice_overhang, …)` trims the anchor alignment back by up to
   `max_splice_overhang` bp from its end; `untrimmed_score = opt.score() − trimmed_off_score`.
2. A `SpliceRegion` (`src/splicing.cpp`) scans a `2 * max_splice_overhang` window around the trimmed
   end for positions matching a **listed** motif (`candidate_splice_sites(motif_num)`).
3. Each candidate = a `PutativeJoin` of a left `PrejoinSide` + right `PrejoinSide` at a motif site,
   building a `JoinedSpliceGraph` with the intron edge; the **connecting alignment** re-aligns only
   the trimmed `~max_splice_overhang`-bp window to that graph (`align_global_banded`, ~:3474).
4. Selection (`:3459`-`:3519`): the candidate with the best `net_score` wins, where
   `net_score = motif_score + untrimmed_anchor_score + connecting_aln.score() + intron_score − opt.score()`.

Key point: `net_score` is **already whole-read-relative** (anchors + bridge + priors − unspliced
optimum), so "score the whole read" is not the missing piece. What is local is the **placement DP**:
the connecting alignment sees only ~`max_splice_overhang` bp of exact-match context, whereas STAR's
whole-read DP sees the full 150 bp anchors and so pins the junction where the exact match ends.

## Root-cause hypotheses (CONFIRM in Phase 1 — do not assume)
- **H1 — limited placement context.** The connecting-alignment window (~`max_splice_overhang` bp,
  empirically ~6-8 under `-n rna -B`) is too short to pin the junction when the near-junction bases
  are ambiguous; STAR's full-anchor context resolves it. Predicts: enlarging the window fixes the 5.
- **H2 — equal-prior motif budget.** Relaxed `--splice-motif-scores` lists many non-canonical motifs
  at equal prior, so a 1-2 bp-off site with a listed motif ties the true site on `motif_score`, and a
  chance connecting-alignment gain tips selection. Predicts: the off-site's motif is a *different*
  listed motif and `motif_score` is equal; the connecting-aln delta is tiny.
- **H3 — intron-length prior.** `intron_length_score` differences bias a slightly different length.
  Predicts: `intron_score` differs between true and off site (unlikely to matter at ±1-2 bp on ~800 bp).
- **H4 — trimming bias.** `trimmed_end` trims to a position that systematically offsets the search.

## Phase 1 — DIAGNOSE the 5 high-support mis-placements (instrument only, no algorithm change)
For each of the 5, dump every candidate `PutativeJoin` considered and, for the TRUE site vs the WINNING
off-site, the `net_score` components (`motif_score`, `connecting_aln.score()`, `intron_score`,
`untrimmed_score`) and the exact-match anchor length on each side. Reuse `--trace-splice-search`
(existing PR1 trace) or add a small targeted dump behind a debug flag. **Deliverable:** which of H1-H4
dominates and by how many score points the off-site wins. The mechanism chosen in Phase 2 is dictated
by this, not guessed.

## Phase 1 RESULT (2026-07-10)
`--max-splice-overhang` sweep on the clean control (default 16): 16 → canon 14/15, nc recall 25/30,
prec 50% (30/60); 30 → 15/15, **27/30**, 35% (35/99); 60 → 11/15, 2/30, 10%; 100-150 → collapse.
**H1 confirmed but the knob couples three effects** — trim depth (+recall via exposing mis-anchored
junctions), motif-search width `2×overhang` (+spurious → −precision), and over-trim anchor breakage
(>60 collapses). Recall and search width are geometrically coupled (exposing a junction T bp back
needs a ~2T search to reach it), so this knob is NOT the precision fix. The **precision** gap (5
high-support mis-placements) lives at default, where the true and off sites are BOTH already
candidates — so it is a placement/scoring problem (pick the right existing candidate), separable from
search width. Next Phase-1 datum: a per-candidate-join score dump at default to decide H1-context vs
H2-motif-tie for those 5, which dictates the scoring change.

## Phase 1 VERDICT (2026-07-10, `--sj-candidates` dump)
Instrumented every gate-passing candidate join (`--sj-candidates`: read, donor/acceptor, motif,
motif_score, connect_score, intron_score, net_score) and analyzed the clean control (default overhang).
- For the 51 reads that had BOTH the true and a false site as candidates but picked false: the winner
  beats the true site by **net +3.0 median, 84% carried by connect_score, motif_score Δ = 0** → the
  local **connecting-alignment score** decides, NOT the motif prior (H2 ruled out for ties).
- For the 10 high-support false clusters (support 41-69, the precision drivers): **94% of their reads
  never had the true site as a candidate**; the winning sites sit 1-4 bp from a true junction and their
  motifs are ~half **canonical GT-AG/GC-AG** (a canonical dinucleotide near a non-canonical truth
  junction wins on the 0.98/0.05 prior — "canonical-steal") and ~half non-canonical (connect jitter).
- **Unifying root cause:** the placement DP sees only ~16 bp of context, so a 1-4 bp shift incurs too
  little alignment penalty to overcome either the canonical motif prior or connect noise. STAR's
  whole-read alignment makes the shift penalty dominate and pins the true site. **One fix — whole-read
  placement context — addresses both sub-mechanisms.** Confirms the primary design below.
- Instrumentation added (default-off, tests pass): `--sj-candidates FILE`, plus `--max-splice-overhang N`
  (Phase-1 knob; couples trim/search/context and is non-monotonic — not the fix).

## Phase 1 FOLLOW-UP: cheap levers exhausted → full re-architecture chosen (2026-07-10)
After the verdict, every cheap precision lever was tested and FAILED or backfired on the clean control:
`--max-splice-overhang` widening (35%, more spurious; high-support false 5→17), canonical-prior
flattening (STAR-flat direction: 49%/40%, no help), overhang/unique filters (no separation),
distance-collapse (47→63% but recall 23→19). **True positives are invariant at 30 across every config;
levers only move the spurious count.** mpmap finds MORE true junctions than STAR (30 vs 24) but pays
with per-read scatter — higher recall and lower precision are two sides of the same per-read-placement
architecture. Root mechanism (multipath_mapper.cpp:3459): the splice-join selection is a
branch-and-bound that PRUNES candidates whose score bound < current best, then keeps the single best
by `net_score` (motif prior + local connecting alignment). A high-scoring near-miss site (canonical
prior or connect noise) can prune the true non-canonical site before it is aligned, and different reads
prune/pick differently → scatter. **User decision: full whole-read re-architecture** (adapt STAR's
whole-read best-window + one-junction-per-cluster to graphs).

## Phase 2 — DESIGN: whole-read best-window re-architecture (graph-native)
Target (STAR-faithful): for each read, consider all candidate splice windows, align the WHOLE read
(both exons + intron at the candidate site) in each, pick the single best whole-read spliced alignment,
report its junction. Consistent whole-read alignment → reads on a true junction converge → one junction
per cluster → precision, without dropping the true placement (recall preserved). All flag-gated;
default byte-identical; validated against the no-regression gate + chr20-10x + MHC + tests.

**Milestones**
- **M1 — evaluate-all (output-invariant):** `--splice-eval-all` removes the score-bound pruning so the
  true site is never discarded before alignment. Pruning only skips candidates that cannot beat the
  best, so the winner (and thus default output) is unchanged; this only exposes the full candidate set.
  Combined with `--sj-candidates`, confirms whether the true site is generated-but-pruned/sub-threshold
  (→ re-scoring can recover it) or never generated (→ candidate generation must change). [IMPLEMENTED]
- **M2 — whole-read re-score selection [IMPLEMENTED, commit `8dfa2db`, `--splice-whole-read`]:**
  for each spliced read, re-rank the candidate joins by a WHOLE-READ score instead of the local
  `net_score`. Build a small two-exon DAG (donor exon ending at the junction -> acceptor exon
  starting at it, read-length context each side), locally align the whole read to it, add a rescaled
  STAR fixed motif bonus. **Key calibration finding:** STAR's `scoreGapNoncan=-8` is on STAR's scale;
  on mpmap's ~1/base scale it over-dominates and canonical-steals (a nearby GT-AG beats the true
  non-canonical junction, recall 25->22), while weight 0 slightly over-reports (precision 48%).
  `--splice-whole-read-motif-weight` (default 0.5) is the balance point. RESULT on the clean chr20
  control: canonical recall 14/15 and non-canonical recall 25/30 both HELD (both exceed STAR's 12/15,
  24/30), non-canonical precision **50% -> 52%**; repeat-heavy control no regression; default-off
  byte-identical; tests 33/35 pass. The residual precision gap to STAR's 100% is paralog/repeat
  false positives that align well to their (wrong) two-exon graph — the whole-read score cannot
  reject these (see M3/paralog disambiguation as the next lever, separate from placement).
- **M3 — one-junction-per-cluster reporting:** at `--sj-out`, collapse per-read placements to the
  whole-read-best consensus per cluster (reassign low-support near-duplicates to the dominant true
  junction; never drop, to hold recall).
- **M4 — validate:** no-regression gate (precision AND recall ≥ baseline on clean + repeat-heavy),
  chr20-10x + MHC anti-overfitting, tests 33/35, runtime.

## Paralog false positives — graph-native STAR defense [IMPLEMENTED, commit `e272e6d`]
Separate lever from placement. STAR confirmed defaults (from the 2.7.11b binary): `outFilterMultimapNmax=10`
(drop >10-loci reads), `winAnchorMultimapNmax=50` (repeat seeds don't anchor), `outSJfilterCountUniqueMin=3 1 1 1`
and `outSJfilterCountTotalMin=3 1 1 1` (OR'd), `outSJfilterOverhangMin=30 12 12 12`,
`outSJfilterDistToOtherSJmin=10 0 5 10`, `scoreGapNoncan/GCAG/ATAC=-8/-4/-8`. Root cause in mpmap:
it hit-caps repeat MEMs, never enumerates the paralog copies, and **mislabels 100% of paralog false
junctions as uniquely-supported** (measured: 869/869 on the repeat-heavy control). Fix (two composable
flags): `--sj-anchor-multimap-max N` (graph-native winAnchorMultimapNmax — query GCSA2 for the junction's
donor/acceptor anchor k-mer frequency; if > N loci, count support as multi not unique) and
`--sj-min-unique M` (outSJfilterCountUniqueMin). Result: repeat-heavy precision 3% -> 16% (threshold 10,
891 -> 119 junctions) by removing repeat-anchored junctions; genome-unique control near-no-op; default-off
byte-identical. Tuning: anchor k-mer is min(gcsa order, 24) — shorter than the k=50 uniqueness screen, so a
sub-k-mer can recur inside a 50-mer-unique flank and drop a few true junctions on the clean control (use a
longer anchor k-mer / higher threshold); the GCSA count adds per-junction runtime (bound/cache it).
Primary design (STAR-faithful, expected if H1/H2 dominate): **`--sj-whole-read-window`** — when placing
the splice, score candidate placements by the **whole-read** fused alignment (full anchors + bridge),
not just the trimmed window, and **tie-break toward maximal exact-anchor extension** (STAR pins the
junction where the exact match ends). Concretely one or both of:
- (a) Enlarge the connecting-alignment / SpliceRegion window toward the full anchor so exact-match
  context pins the junction; bound the window to control runtime.
- (b) Add a deterministic tie-break: among candidates within `net_score` epsilon, choose the one that
  maximizes exact-match anchor length (equivalently, minimizes near-junction mismatches), independent
  of the equal motif prior. This directly counters H2 and yields consistent cross-read placement.

Secondary (only if needed and non-regressing): cluster-consensus at report time — reassign
low-support near-duplicate junctions to the dominant nearby junction (reassign, not drop, to preserve
recall). Cleans the ~19 singletons without touching placement.

## Phase 3 — IMPLEMENT (flag-gated, default byte-identical)
- Hidden/advanced flag `--sj-whole-read-window` (and a knob for the window/epsilon if needed).
- Localize edits to `test_splice_candidates` placement/selection; do not alter the default code path.
- Keep the donor-coordinate reporting fix (`8b6115c`) intact.

## Phase 4 — VALIDATE (the no-regression gate; this is the acceptance test)
On the clean chr20 control, baseline (flag off) vs flag-on, all four metrics:
- **GATE (must pass to ship):** flag-on canonical recall ≥ baseline, non-canonical recall ≥ baseline,
  non-canonical precision ≥ baseline, mapping accuracy ≥ baseline. Any regression ⇒ do not ship, iterate.
- **Target:** meet/beat STAR on non-canonical precision and recall simultaneously.
- Guard against overfitting the 45-junction control: also run (i) the repeat-heavy control, (ii) a
  real-data sanity (chr20 10x or MHC), (iii) tests `33_vg_mpmap.t` + `35_vg_mpmap_trace.t`
  (default-off byte-identical). Report runtime delta.

## Risks & fallback
- **Regression risk** to canonical/mapping from changing placement → flag-gating + the no-regression
  gate + the test suite catch it before shipping.
- **Overfitting** the small control → real-data + repeat-heavy sanity in Phase 4.
- **Runtime** from a larger placement window → bound the window; measure; keep epsilon-tie-break (cheap)
  as the minimal variant.
- **Fallback** if the deep placement fix cannot clear the no-regression gate: ship the secondary
  consensus-reporting + motif-strand-consistency (lower ceiling, but non-regressing), documented as the
  safe alternative. Do not ship anything that regresses either metric.

## Review decisions (2026-07-10)
1. **Phase-1-first: APPROVED** — diagnose the mis-placement mechanism before designing the fix.
2. **Phase-4 real-data sanity: BOTH** chr20 10x `gex` and MHC pangenome fixtures gate shipping.
3. Runtime budget: STAR-parity target ~+a few %; bound the placement window; measure.

## Phase 1 execution note
`max_splice_overhang` defaults to `2*max_softclip_overlap = 16` (placement-DP context ~16 bp/side vs
STAR's 150 bp) and is currently NOT CLI-tunable. First Phase-1 probe: expose it as `--max-splice-overhang N`
and sweep on the clean control. If enlarging the context window resolves the 5 high-support
mis-placements without regressing recall, H1 (limited context) is confirmed and the flag is the fix
lever; if not, add a per-candidate-join score dump to test H2 (equal-prior motif tie). Caveat: raising
it also widens the `2*overhang` motif search, which can admit more spurious candidate sites — watch
precision/recall jointly across the sweep.
