# MMP -> STAR Parity: Follow-up Implementation Plans

## Implementation status (2026-07-08, branch `mmp-splice-seeding`)

All three plans are IMPLEMENTED, flag-gated, default-off (default mapping output
byte-identical), and verified on the de-novo linear MHC graph. Tests: `35_vg_mpmap_trace.t`
25/25, `33_vg_mpmap.t` 25/25.

- **A (sequential chaining) -- DONE.** `--mmp-chain` / `--mmp-max-seeds N`. `emit_chain()` in
  `mpmap_mmp.cpp` re-seeds from the end of each matched segment. 20k de-novo:
  12,984 chain seeds, spliced 323 -> 325, geometry preserved (97.5%).
- **B (motif scoring) -- DONE.** `--splice-motif-scores FILE` (donor acceptor frequency)
  applied via new `MultipathMapper::set_splice_motifs` -> `SpliceStats::update_motifs`
  (mirrors `set_intron_length_distribution`). A default-equivalent table reproduces the
  default exactly (312 = 312); a table with 3 extra non-canonical motifs finds 320 (+8)
  splices -- non-canonical support works. Parser rejects frequencies summing to > 1.0.
- **C (RC right tail) -- DONE.** `--mmp-strand-mode {native|rc|both}`. `run_mmp()` reverse-
  complements the right tail so the backward search is breakpoint-pinned; positions are
  relocated with `reverse_base_pos` (sub-option a) and refined by downstream subgraph
  re-alignment. 20k de-novo: 6,498 RC seeds; `both` mode 323 -> 329 spliced at 97.6%
  geometry, annotated-junction detection preserved (36 -> 36). Sub-option (b) exact
  multi-node relocation remains a reserved refinement.

- **Primary seeding (plan A "option 2") -- DONE.** `--mmp-primary`: a whole-read sequential
  MMP walk (`generate_primary_seeds` in `mpmap_mmp.cpp`) whose seeds AUGMENT the MEM pool
  before clustering, hooked in `multipath_map` / `multipath_map_paired` right after
  `find_mems` (padding `mem_fanouts` to satisfy `record_fanouts`' size assert; MMP seeds have
  no fanouts). Unlike the splice-rescue path this can change the MAPPING RATE. 200k de-novo:
  mapped 32,947 -> 32,985 (**+38 reads**), spliced 2,783 -> 3,276 (**+493, +17.7%**),
  958,040 primary seeds, runtime +9.9%. Trace field `n_mmp_primary_seeds`. Default-off
  byte-identical. This is the only MMP mode that maps previously-unmapped reads (the
  splice-rescue modes only refine already-mapped reads).

Large-scale validation (200,000 de-novo reads, linear MHC graph, truth junctions):
baseline vs `--mmp-seed --mmp-strand-mode both --mmp-chain`: spliced 2,783 -> 2,953
(**+170, +6.1%**; RC right tail 103,374 seeds + chaining 162,817 seeds), runtime +3.4%
(31.0s -> 32.1s, MMP work 2.2s). Correctness at scale: annotated-junction detection
PRESERVED (368 -> 368) and genuine-geometry 97.9% -> 98.0% -- the +170 extra splices are
valid structural splits at NOVEL (non-annotated) positions. No crash; all three features
compose. Tests 35_vg_mpmap_trace.t 25/25, 33_vg_mpmap.t 25/25, default-off byte-identical.



The shipped STAR-style MMP seeding path (see `secondaryPeakVerification.md`) matches STAR's
breakpoint-anchored re-seed on the LEFT soft-clip tail, reuses mpmap's validated splice
scoring, and adds a correct (but distal-pinned) RIGHT tail. Review identified three
algorithmic gaps between that path and STAR proper. Each plan below is flag-gated,
disabled by default, and keeps `multipath_mapper.hpp` untouched, following the existing
`mpmap_mmp` pattern.

Baseline facts (grounded in the current tree):
- Generator: `src/mpmap_mmp.cpp` (`generate_mmp_seeds`), emits one maximal seed per tail.
- Rescue driver: `find_spliced_alignments` (`src/multipath_mapper.cpp:4042` single,
  `:4303` paired); the anchor loop at `:4109` deliberately omits `++j`, so a soft-clip
  created by a splice is re-rescued on a later iteration (mpmap already iterates).
- Splice scoring: `SpliceStats` (`src/splicing.hpp:28`), `motif_score(motif_num)` (`:57`),
  `intron_length_score(length)` (`:61`), combined in `post_align_net_score`
  (`src/multipath_mapper.cpp:2896`); motif enumeration in `MotifPairIterable` (`:2906`).
- Position helpers: `reverse_base_pos(pos, node_len)` (`src/types.hpp:89`),
  `reverse_complement(string)` (`src/utility.hpp:30`), `make_pos_t(gcsa::node_type)`
  (`src/position.cpp:13`), `xindex->get_length(...)`.

---

## Plan A: Full sequential MMP walk (chaining)

**Current state.** `generate_mmp_seeds` emits a single maximal seed per soft-clip side.
Multi-junction reads are handled only via mpmap's re-rescue (`:4109` `j`-loop). What is
missing is a *chain* of positioned seeds within a tail, the way STAR walks MMP1 -> MMP2 ->
MMP3 across a read.

**Two designs.**
1. *In-generator chaining (localized; recommended first).* Loop inside
   `generate_mmp_seeds`: after emitting the first MMP, restart the backward search from the
   end of the matched segment (`range = full; continue`, mirroring the reseed in
   `find_mems_simple`, `src/mapper.cpp:143`), emitting each successive MMP until the tail is
   consumed or a `--mmp-max-seeds` cap. New flags `--mmp-chain`, `--mmp-max-seeds`; new
   trace fields `n_mmp_chain_seeds`, `n_mmp_chain_depth`. `test_splice_candidates` already
   accepts multiple candidates, so intermediate-exon seeds flow through unchanged.
2. *True sequential front end (invasive).* A from-scratch MMP walk over the whole read,
   producing a seed chain fed as a synthetic cluster before MEM clustering -- a new seeding
   path parallel to `find_mems`. Higher STAR fidelity, but touches core mapping flow.

**Key subtlety.** mpmap tests each candidate against the *primary* pairwise, so a
two-junction read still needs two successive rescue passes to stitch both junctions
(mpmap's `j`-loop iteration provides this). Option 1 surfaces the intermediate seeds; the
multi-junction *stitching* still rides on mpmap's re-iteration.

**Verification.** Identify reads spanning >=2 annotated junctions; measure both-junction
`did_splice` with chain on/off via the truth-join.

**Files.** `mpmap_mmp.cpp` (loop), `subcommand/mpmap_main.cpp` (flags), `mpmap_trace.*`
(chain metrics), `test/t/35_vg_mpmap_trace.t`.

**Risk.** Hit explosion from short chained seeds -> cap with `max-seeds` + `hit_max` +
`min_prefix`; watch `time_mmp_seed_usec`.

**Recommendation.** Option 1 first (localized, gated, measurable); escalate to Option 2
only if the iterative-rescue stitching proves insufficient for multi-junction reads.

---

## Plan B: STAR's motif-dependent junction scoring

**Reframe -- mpmap already does this.** `SpliceStats` ships default canonical motifs with
frequencies (GT-AG 0.9924, GC-AG 0.0069, AT-AC), exposes `motif_score` and
`intron_length_score`, and combines them in `post_align_net_score`. Motif-dependent +
intron-length scoring already exists and is frequency-tiered like STAR's canonical /
semi-canonical ordering.

**The two real gaps to STAR.**
1. *Non-canonical junctions.* `SpliceStats::motif_data` holds only the 3 (semi-)canonical
   pairs, so a fully non-canonical dinucleotide junction is never scored/accepted. STAR
   accepts them with a large penalty. Add a wildcard non-canonical motif class (any
   donor/acceptor pair) with a configurable penalty; `MotifPairIterable`
   (`multipath_mapper.cpp:2906`) extends to include it.
2. *Penalty calibration.* Map mpmap's `motif_score` values + intron model onto STAR's
   scheme (canonical 0, non-canonical/GC-AG/AT-AC tiers, `sjdbScore`). Make the motif table
   configurable via `--splice-motif-scores FILE` or a `--splice-motif-preset star`.

**Design.** Extend the `SpliceStats` constructor to accept a configurable motif table
(donor, acceptor, score) including a non-canonical wildcard; add the config path in
`mpmap_main`. No control-flow change -- `SpliceStats` is already a member.

**Verification.** Regression that canonical GT-AG scores are unchanged; a constructed
non-canonical-junction read is now scored/accepted with the higher penalty; compare the
junction-motif distribution to STAR's `SJ.out.tab` on the same reads.

**Files.** `splicing.{hpp,cpp}` (motif table + non-canonical class),
`subcommand/mpmap_main.cpp` (config flag), test.

**Risk.** Non-canonical acceptance raises false splices -> tune the penalty high
(STAR-like), measure precision via the truth-join.

---

## Plan C: Right-tail breakpoint-pinned via reverse-complement

**Goal.** Replace the distal-pinned right tail with a true breakpoint-pinned seed, matching
STAR (immune to 3' quality decay and downstream junctions).

**Approach.**
1. RC the right tail: `reverse_complement(seq.substr(break, tail_len))`. In RC space the
   breakpoint moves to the right end, so the existing backward-search-from-the-end code is
   natively breakpoint-pinned; the matched suffix of the RC = RC of the forward tail's
   prefix from the breakpoint.
2. `locate` -> nodes; `make_pos_t` gives the position of the RC match's START base = the
   forward seed's distal-most base, on the reverse strand.
3. *Position relocation (the crux).* The seed needs `mem.begin`'s position (the breakpoint
   base); `locate` gives the other end. Two sub-options:
   - (a) *Re-alignment slack (pragmatic, do first).* Emit
     `reverse_base_pos(make_pos_t(node), node_len)` (correct node + strand, possibly the
     wrong end for multi-node matches). `query_cluster_graphs` extracts a subgraph and
     re-aligns `[break, break+L)`, recovering the exact placement when extraction slack
     covers `L`. Cheapest.
   - (b) *Graph walk (exact).* From the located handle, traverse the matched path forward
     `L-1` bases (exact match => unique branch, disambiguated by the read) to the breakpoint
     base. Exact but needs multi-node walk + branch handling.
4. Gate behind `--mmp-strand-mode {rc|both}` (already parsed); `native` stays the shipped
   forward path.

**Verification harness (non-negotiable).**
- *Unit:* tiny graph, known right-junction read, `--mmp-strand-mode rc`; assert the
  relocated position round-trips -- reading the graph sequence there equals the read tail.
  Add to `35_vg_mpmap_trace.t`.
- *Integration:* MHC de-novo + truth junctions; RC-contributed splices must land on
  annotated junctions at >= the forward-tail rate. A wrong strand transform produces splices
  that do NOT match truth -- this is the check that defeats the "silently dropped bad seed"
  failure mode.

**Files.** `mpmap_mmp.cpp` (RC branch + relocation), `mpmap_trace.*` (`n_mmp_rc_seeds`),
test.

**Risk.** Multi-node relocation error -> start with (a), gate on the harness passing; if it
mis-locates at node boundaries, escalate to (b); if verification fails, `native`
(distal-pinned) stays default.

**Recommendation.** (a) first, verify, ship behind `both`; escalate to (b) only if needed.

---

## Suggested order

1. **C's verification harness first** -- it is the highest-value next step because it gates
   right-tail correctness and is reusable for A and B's truth-join checks.
2. **B** -- smallest change (scoring-table extension; algorithm already present), unlocks
   non-canonical junction discovery.
3. **A** (option 1) -- localized chaining for multi-junction reads.
4. **C's exact relocation (b)** and **A's option 2** only if the pragmatic versions fall
   short under verification.
