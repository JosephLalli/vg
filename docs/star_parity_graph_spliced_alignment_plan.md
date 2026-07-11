# STAR-parity spliced alignment on the pangenome graph — design plan

> **Navigation:** Hub / entry point for all splice/SJ docs: [`mpmap_vs_star_sj_STATUS.md`](mpmap_vs_star_sj_STATUS.md).
> Sequence position: follows `whole_read_best_window_plan.md` (M1/M2 shipped); precedes
> `graph_denovo_splice_discovery_plan.md` (Phase 4 discovery spec).

> **Status (2026-07-10):** PROPOSED. Bridges the remaining STAR-vs-mpmap gaps (see
> `whole_read_best_window_plan.md` for what shipped: M1/M2 whole-read re-score + graph-native paralog
> filter). Goal unchanged: adapt STAR's algorithm to the graph and match/beat it on mapping, junction
> discovery, and non-canonical discovery, with **no regression** vs current mpmap.
>
> **Update (2026-07-11) — Phase 4 (stitch-first DP) superseded, not built.** De novo non-canonical
> discovery, the gap Phase 4 targets below, was closed by a parameter/input lever instead (raising
> `--max-motif-pairs`, supplying all-256 motifs via `--splice-motif-scores`, filtering with
> `--sj-min-unique`) — see `beat_star_splice_discovery_implementation.md`. No
> `banded_global_aligner` surgery was implemented. Phases 1/2/3/5/6 below remain open/unimplemented
> proposals as written; only Phase 4's justification for aligner surgery is superseded — the
> non-canonical-precision gap it also cites is real (see STATUS) but the diagnosed fix is the
> whole-read-cluster consensus reporting in `whole_read_best_window_plan.md` M3, not this DP.

## Unifying idea: a splice is a node→node edge
Every gap below closes if a splice junction is treated as a **first-class graph edge / transition**
rather than a linear-genome special case:
- in the **aligner**: a splice is a scored long-gap *edge transition* between motif-bearing graph
  positions (stitch-first, whole-read), traversing variant nodes like any other edge;
- in the **index**: a *discovered* junction is promoted to a real graph edge (two-pass), so pass-2
  alignment crosses it natively (this is what mpmap already calls an "annotated" junction);
- in **multimapping**: a read's loci and a junction's support are counted over **graph positions**,
  from uncapped GCSA2 seed multiplicities.

This keeps the reference as the pangenome node/edge graph throughout — nothing is flattened to a
linear sequence at any stage.

## Invariant to preserve (the constraint)
Every phase must: (a) align over nodes/edges (variant-aware — no 2-node linear collapse);
(b) represent junctions as edges; (c) count loci/NH as graph positions; (d) keep the reference
substrate a graph (pangenome or refpath), never a linear rewrite; (e) be flag-gated so default output
is byte-identical.

## Gap → phase map
| Gap (from the STAR comparison) | Phase |
|---|---|
| M2 re-score collapses to a 2-node linear graph (drops variants) | 1 |
| Discrete candidate sites, no continuous sliding / canonical-microhomology shift | 2 |
| Per-junction anchor-frequency proxy, not per-read genome-wide NH | 3 |
| No read-level multimap dropping (`outFilterMultimapNmax`) | 3 |
| Ported `−8` motif constant, not the aligner's own scale | 6 |
| No two-pass junction insertion (STAR's main non-canonical-recall lever) | 5 |
| Missing `outSJfilterDistToOtherSJmin`, overhang filter; no windowing analog | 6 (filters), 4 (windowing) |
| **Deepest:** align-then-rescue vs STAR stitch-first splice-aware DP | 4 |
| Seeding: MEM vs MMP (proved not the lever) | 7 (optional) |

## Phases (each: mechanism, graph-native form, effort/risk, validation)

### Phase 1 — Variant-aware whole-read re-score  [low effort, low risk]
Bridge the 2-node-linear irony. Replace M2's two-node graph with real exon **subgraphs** extracted by
`extract_extending_graph` (donor backward, acceptor forward) joined by one splice edge, then align the
whole read over that subgraph — so alternate/variant exon nodes participate in the score. Make the
paralog anchor k-mer graph-path-aware (query along the actual traversed path, and lengthen it to match
the ~50 bp uniqueness scale so the clean-control near-no-op becomes an exact no-op). Reuses the M2 seam.
Validate: clean control exact no-op; add a **variant-bearing** control (reads spanning a junction whose
exon carries a SNP/indel node) to prove the re-score uses the variant path.

### Phase 2 — Continuous junction sliding + canonical/annotated shift  [low-moderate, low]
STAR slides a junction across the microhomology region and prefers a canonical / annotated position.
Graph-native: after selecting a junction, walk the donor node's 3′ and acceptor node's 5′ sequences,
enumerate the shift window where donor/acceptor sequence is interchangeable (microhomology), and pick
the shift maximizing (exact-match + fixed motif score), preferring a shift that lands on an **existing
graph edge** (annotated). Improves placement precision and annotated-junction recall. Validate:
precision on clean control; annotated-junction match on the pangenome (CAT edges).

### Phase 3 — Graph-native genome-wide multimapping / true NH  [moderate, moderate]
Replace the per-junction anchor proxy with a real per-read locus count. Root cause: mpmap hit-caps
repeat MEMs and never sees paralog copies. Fix: derive the read's locus count from **uncapped GCSA2
seed multiplicities** (`match_count` = number of graph positions, STAR's suffix-array-count analog)
combined over the read's seeds; add a `outFilterMultimapNmax`-style drop for reads whose graph-locus
count exceeds a cap; feed a true unique/multi split into `--sj-out`. Retire `--sj-anchor-multimap-max`
in favor of this read-level NH (keep the anchor query only as a cheap fallback). Bound runtime by only
uncapping for reads that already trigger splice rescue. Validate: on the repeat-heavy control, confirm
the 100%-mislabeled-unique paralog junctions flip to multi; a genuine paralog control (MHC) for the
precision win; mapping-rate delta from the multimap drop.

### Phase 4 — Stitch-first splice-aware whole-read graph DP  [HIGH effort, HIGH risk]
The deepest gap. Add a splice-aware alignment over a **spliceable subgraph**: cluster seeds (existing),
then gather spliceable neighbor subgraphs within the max-intron **graph distance** (snarl-distance
index — the graph analog of STAR's genomic windows), and run one whole-read DP that permits **splice-gap
edge transitions** at motif-bearing positions, scored with the fixed motif penalty + intron-length
prior, traversing variant nodes. This makes splicing native to the aligner (no soft-clip-then-rescue)
and subsumes Phases 1–2's re-score/slide into the DP itself. Highest risk; gate strictly, and only after
1–3 validate. Options to bound cost: restrict the splice-gap transitions to motif positions found in the
spliceable window; cap window size by intron-length model. Validate: full no-regression gate + runtime
budget; compare against the M2 re-score it replaces.

### Phase 5 — Two-pass junction-edge insertion  [moderate, moderate] — biggest recall ROI
STAR's non-canonical recall comes largely from 2-pass. Graph-native and elegant: Pass 1 maps + emits
`--sj-out`; promote each discovered junction to a real **graph edge** (donor node:offset → acceptor
node:offset) via the existing `vg rna` / edge-insertion machinery; refresh a **lightweight junction-edge
overlay** (avoid a full GCSA/dist rebuild — only the new edges + a distance patch). Pass 2 remaps: the
junctions are now native edges, so reads cross them without rescue, and mpmap already scores graph-edge
junctions as annotated (`--sjdb-score`). This is STAR's `sjdbInsert` but as edges, not a linear sjdb.
Do this early (after 1–2) — likely the largest single recall gain, especially non-canonical. Validate:
non-canonical recall pass-1 vs pass-2 on the clean control; annotated-junction traversal; overhead of
the overlay refresh.

### Phase 6 — Calibration + STAR-parity filters  [low-moderate, low]
Derive the motif penalties in the **aligner's own score units** (from its match/mismatch scale) instead
of the ported `−8` (which needed the 0.5 rescale) — a principled `scoreGapNoncan` in mpmap units.
Implement the remaining `outSJfilter*` in the collector: `outSJfilterDistToOtherSJmin` (needs
donor/acceptor genomic coords — add a node→refpos map or a graph-distance check in `close()`) and the
per-class overhang filter; ship a `--sj-star-filter` preset matching STAR's default vectors. Validate:
STAR-parity comparison with matched filters on both controls.

### Phase 7 — Seeding parity (optional)  [low priority]
MMP-over-GCSA already exists (`--mmp-primary`, proved not the lever for short reads). Keep as an option;
revisit only for long-read / density-sensitive regimes.

## Sequencing and rationale
1. **Phases 1, 2, 6** — low-risk, near-term, compose with what shipped; land these first to solidify the
   junction-level fidelity and calibration.
2. **Phase 5 (two-pass)** — highest recall ROI, moderate effort, graph-native fit; do it early.
3. **Phase 3 (multimapping/NH)** — precision root cause for paralogs; the real replacement for the
   shipped anchor-frequency proxy.
4. **Phase 4 (stitch-first DP)** — deepest and riskiest; last, strictly gated, once 1–3 validate it may
   subsume the re-score entirely.

## Cross-cutting: validation harness and invariants
- Every phase flag-gated; default `--sj-out`/mapping byte-identical (the shipped discipline).
- No-regression gate: precision AND recall ≥ current mpmap on the **clean** and **repeat-heavy** chr20
  controls; add (a) a **variant-bearing** control (exercises the node/edge model), (b) the **MHC**
  paralog control, (c) a **two-pass** benchmark (pass-1 vs pass-2 non-canonical recall). Metrics:
  mapping accuracy, canonical/non-canonical recall, non-canonical precision, NH/multimap behavior,
  runtime.
- Graph invariant checked per phase: no linear collapse; junctions are edges; loci counted as graph
  positions.

## Key risks / open questions
- Phase 4 DP cost scales with spliceable-subgraph size → bound by intron-length model + snarl-distance
  windowing; profile before committing.
- Phase 5 index refresh must be incremental (junction-edge overlay + distance patch), not a full rebuild,
  or the two-pass cost dominates.
- Phase 3 uncapped multiplicity must not reintroduce the runtime that hit-capping was added to avoid →
  uncap only for splice-rescue-triggering reads.
- Motif calibration (Phase 6) interacts with Phase 2's sliding and Phase 4's DP — calibrate once, in
  aligner units, and reuse everywhere.
