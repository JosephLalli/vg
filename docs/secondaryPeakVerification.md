# Implementation Plan: STAR-style MMP Splice-Junction Discovery Path for `vg mpmap`

## Implementation Status (2026-07-07, branch `mmp-splice-seeding`)

Follow-up plans to close the remaining gaps to STAR (sequential chaining, motif scoring,
reverse-complement right tail): see `mmp_star_parity_plans.md`.

- **M1 Module + flag scaffold — DONE.** New TU `src/mpmap_mmp.{hpp,cpp}` (config gate,
  pointer-stable `thread_local deque` seed store, generator). Five hidden flags in
  `mpmap_main.cpp` (`--mmp-seed`, `--mmp-min-prefix`, `--mmp-max-intron`, `--mmp-hit-max`,
  `--mmp-strand-mode`). Gated hook in `identify_unaligned_splice_candidates`; `multipath_mapper.hpp`
  untouched. Verified default-off output byte-identical.
- **M2 Left-tail native generator — DONE.** GCSA2 backward-search MMP walk pinned at the
  breakpoint, keep-short, capped `locate`, deduped, feeds existing rescue. `reset_read_store()`
  wired into both `multipath_map` entry points. Distance-pruning deferred to `test_splice_candidates`.
- **M4 Instrumentation — DONE.** Trace schema bumped to v2; fields `n_mmp_seeds_generated`,
  `n_mmp_seed_hits`, `n_mmp_seeds_new` (not in raw-MEM pool), `n_mmp_seeds_kept_short`,
  `time_mmp_seed_usec`. Populate only under `--mmp-seed`.
- **M6 Paired-end — DONE (no code).** The hook is shared via `identify_unaligned_splice_candidates`,
  which both single and paired `find_spliced_alignments` call; paired records carry the fields.
- **Tests — DONE.** `35_vg_mpmap_trace.t` extended to 20/20 (adds schema-v2, field-presence,
  default-off isolation, mapping-intact). `33_vg_mpmap.t` regression 25/25 (splice tests pass).
- **M3 Right-tail generator — DONE (forward/distal-pinned).** The generator now handles BOTH
  soft-clip tails via GCSA2 backward search: left tail is the maximal suffix ending AT the
  breakpoint (junction-pinned); right tail is the maximal suffix ending at read_end
  (distal-pinned). Both use `make_pos_t` on the located node, which already yields the seed's
  first-base position on the correct strand -- so no manual strand math and no correctness risk.
  A true breakpoint-pinned RIGHT seed would need reverse-complement querying with multi-node
  position relocation (GCSA `locate` returns only the match-start node, so recovering the
  breakpoint end of a multi-node match requires graph traversal); that refinement is documented
  and reserved behind `--mmp-strand-mode` (`native` = both tails as shipped; `rc` = left-only
  placeholder until the RC path is built and verified). This deliberately ships the correct,
  verifiable right tail rather than an unverifiable RC transform.
- **M7 Acceptance-gate relaxation — DONE (MMP-specific, opt-in).** New `--mmp-relax-accept N`.
  `mpmap_mmp::is_mmp_seed()` (a thread_local pointer set kept in sync with the seed store) lets
  `align_to_splice_candidates::consider_candidate` apply a relaxed `min_softclip_length` ONLY to
  MMP-sourced candidates; raw candidates and any run with `relax_accept == 0` are unchanged. This
  converts short-overhang visibility into acceptance without touching the raw-MEM path's selection.

### Empirical A/B and verification (de-novo linear graph)
- **200k reads, left-tail only, gates on:** spliced 2,783 -> 2,812 (**+29, +1.04%**); 103,003 MMP
  seeds across 16,118 reads, 96,336 (93.5%) NOT in the raw-MEM pool; runtime +2.1%.
- **20k reads, both tails (M3):** spliced 312 -> 316 (left-only) -> **323 (both tails)**, i.e. the
  right tail adds +7; 18,489 seeds (7,525 from the right tail), 17,410 new.
- **M3 correctness (truth-join, 20k):** MMP splices keep 97.5% genuine complementary-half geometry
  (vs 97.4% baseline) -> the right-tail seeds produce valid splices, not corruption. BUT the
  annotated-junction match count is unchanged (36 -> 36): MMP's +11 extra splices land on
  NON-annotated positions (the MHC paralogy pattern), so MMP surfaces more splices without
  recovering more TRUE junctions. Direct evidence that MEM seeding already captures the
  annotated-junction-recoverable reads.
- **M7 effect:** none on this data (spliced 324 == 324 even at `--mmp-min-prefix 3 --mmp-relax-accept 1`).
  The acceptance LENGTH gate (`min_softclip_length_for_splice` ~= 4) is never the binding filter for
  MMP candidates -- their local alignments cover enough regardless of seed length; downstream
  filtering is dominated by distance + motif, not length. M7 is correctly a no-op when
  `relax_accept >= gate`, and wired (applies only to `is_mmp_seed` candidates).

Overall: implementing the STAR alternative (both tails + relaxable acceptance) confirms the trace's
verdict -- MEM seeding already captures ~99% of the junctions this data can recover; the STAR-style
front end adds visibility (10x+ candidates) and a small number of mostly-non-annotated splices for
+~2% runtime. It does not overturn the "keep MEM seeding" conclusion.

## Scope and Framing

Add an experimental, flag-gated, instrumented **Maximal Mappable Prefix (MMP)**
seed-generation path that feeds STAR-style breakpoint-pinned acceptor/donor seeds into
`vg mpmap`'s existing splice-rescue machinery, as an alternative to the current
soft-clip-gated MEM candidate surfacing. Default output must be byte-identical when the
flag is off. The change reuses and extends the existing trace layer
(`src/mpmap_trace.{hpp,cpp}`, `test/t/35_vg_mpmap_trace.t`).

## GCSA2 Feasibility of the Crux

The crux has three sub-questions. Evidence from the actual code:

**(a) Re-seeding from an arbitrary read offset — SUPPORTED.**
The GCSA2 public `find()` takes an arbitrary `[begin, end)` and starts a fresh search
(`include/gcsa/gcsa.h:97-121`). The mapper already restarts a search mid-read by
resetting the range to the full BWT interval and continuing from a chosen cursor:
`find_mems_simple` does `range = full_range; --cursor;` (`src/mapper.cpp:143-144`), and
`find_fanout_mems` does `range = full_range;` at a breakpoint (`src/mapper.cpp:446`).
`accelerate_mem_query` returns the full range `(0, gcsa->size()-1)` when acceleration is
not applicable (`src/mapper.cpp:1899-1914`; declared `src/mapper.hpp:340`). So a fresh
MMP starting at any offset `j` is directly expressible.

**(b) Single-character extension along graph successors — SUPPORTED, but backward only.**
The one-base extension primitive is `gcsa->LF(range, comp)` (`include/gcsa/gcsa.h:155-159`),
used at `src/mapper.cpp:132, 418, 1024`. Critically, GCSA2 is **backward-search only**:
`find()` initializes from the *last* character (`charRange(*end)`) and applies `LF`
walking right-to-left (`include/gcsa/gcsa.h:102-108`); the "bidirectional iterators"
comment at `gcsa.h:89` refers to C++ iterator traversal, not bidirectional BWT search.
The MEM walk in `find_mems_simple`/`find_mems_deep` confirms this: the cursor starts at
`seq_end - 1` and decrements toward `seq_begin` (`src/mapper.cpp:124-166, 1024-1028`).
**Consequence:** a match is grown by extending its *left* boundary while its *right*
boundary stays fixed — the opposite of STAR's forward (5'->3') MMP, which fixes the left
boundary and grows rightward. This asymmetry is the central design finding.

**(c) Retaining short terminal matches as positioned seeds — PRIMITIVE yes, POLICY no.**
`gcsa->count(range)` and `gcsa->locate(range, [hit_max,] nodes)` work on any non-empty
range at any length (`gcsa.h:124-128`), returning `node_type` hits (id+orientation via
`make_pos_t`, as at `src/multipath_mapper.cpp:4011-4015`). But the existing finders
**discard** short matches by policy: `find_mems_deep`/`find_mems_simple` filter by
`min_mem_length` and SMEM-ness and apply `hit_max`/`hard_hit_max` caps
(`src/mapper.cpp:192-215, 988-998`; members at `src/mapper.hpp:274-281`), and the reseed
logic keeps only more-frequent sub-MEMs. Therefore a **new query path with a keep-short,
breakpoint-pinned policy is required**; it cannot reuse `find_mems_deep` unchanged, but
it is built entirely from the *same* primitives (`accelerate_mem_query`, `gcsa->LF`,
`gcsa->count`, `gcsa->locate`, `lcp` optional).

### Resolution of the forward/backward asymmetry (the key design decision)

Junction discovery is symmetric, so the native backward-search primitive is sufficient
if we choose which read end anchors each MMP:

- **Left soft-clip tail** `[0, interval.first)` (missing upstream partner): run native
  backward search from the breakpoint `interval.first` extending left. The breakpoint is
  the pinned *end* of the seed; it grows into the tail. No reverse-complement needed.
  Directly uses `gcsa->LF`.
- **Right soft-clip tail** `[interval.second, read_len)` (missing downstream partner): to
  pin the seed's *start* at the breakpoint (STAR's short-overhang property), extension
  must go rightward. With a backward-only index this requires **reverse-complementing the
  read tail** `R[interval.second..read_len)` and backward-searching that, then flipping
  the located positions' strand. Anchoring at the far end (`read_len`) instead would pin
  the wrong coordinate and lose short overhangs when the distal end mismatches.

Recommended staging: implement the **left-tail native** path first (no RC, lowest risk),
then add the **right-tail RC** path. `NEEDS VERIFICATION`: the exact GCSA2 double-strand
construction and the strand semantics of `locate()` output for RC queries were not
inspected in the index-build code (out of the mapper's scope); validate empirically that
RC-query hits map to the correct opposite-strand graph positions before relying on the
right-tail path.

## Non-Invasive Integration Pattern

The trace layer achieves zero `multipath_mapper.hpp` edits because it only *observes*:
its helpers are file-static free functions taking already-computed public data
(`src/multipath_mapper.cpp:78-300`), routed via a `thread_local` sink pointer
(`src/mpmap_trace.cpp:52-55`; `mpmap_trace.hpp:226-238`).

The MMP path differs in one unavoidable way: **it must call GCSA2 primitives on the
mapper's own indices**, which are `protected` members of `BaseMapper` (`gcsa`, `lcp`,
`accelerator` at `src/mapper.hpp:363-365`; `distance_index`, `xindex`). A free function
cannot reach those. The pattern that preserves the spirit and keeps
`multipath_mapper.hpp` untouched:

- New translation unit **`src/mpmap_mmp.{hpp,cpp}`** holding: a runtime gate
  `bool mmp_enabled()` + config accessors (set once from `mpmap_main`, exactly like
  `mpmap_trace::open`), a `thread_local` **stable backing store** for synthesized
  `MaximalExactMatch` objects, and the generator free function
  `generate_mmp_seeds(gcsa::GCSA*, gcsa::LCPArray*, MEMAccelerator*, SnarlDistanceIndex*,
  PathPositionHandleGraph* xindex, const Alignment&, pair<int64_t,int64_t> interval,
  bool do_left, const pos_t& anchor_pos, const MmpParams&,
  vector<pair<const MaximalExactMatch*, pos_t>>& hit_candidates_out)`.
- The **only edit to existing mapper source** is a small gated call block inside the
  existing member `MultipathMapper::identify_unaligned_splice_candidates`
  (`src/multipath_mapper.cpp:3896`), right after the raw-MEM hit loop
  (`src/multipath_mapper.cpp:3998-4033`). Because that call site is a member, it can pass
  `gcsa, lcp, accelerator, distance_index, xindex` into the free function. This mirrors
  how `mpmap_trace::fill_proactive` receives `mems`/anchors as parameters
  (`src/multipath_mapper.cpp:630-635`).
- `multipath_mapper.hpp` is **not** modified (no new members, no signature change). This
  matches the trace precedent.

Honest caveat: unlike the pure-observation trace layer, this path *does* change candidate
generation (behavior), so "non-invasive" here means "isolated behind a default-off gate
and confined to one new TU + one gated call block," not "zero behavioral effect."

**Lifetime hazard (must handle):** `hit_candidates_out` stores
`pair<const MaximalExactMatch*, pos_t>`, and downstream `align_to_splice_candidates`
dereferences the `MaximalExactMatch*` via `candidate_id_t` (`get<2>` at
`src/multipath_mapper.cpp:3529, 3599`; type at `src/multipath_mapper.hpp:227`).
Synthesized MEMs must outlive the whole rescue for the current read. Use a
`thread_local std::deque<MaximalExactMatch>` (pointer-stable across appends) cleared at
the top of each `multipath_map`/`multipath_map_paired` call, not a `vector`
(reallocation would dangle the pointers). This is the single highest-risk detail.

## Where the MMP Seeds Attach (exact integration points)

1. **Candidate surfacing (primary hook):** append MMP seeds to `hit_candidates_out`
   inside `MultipathMapper::identify_unaligned_splice_candidates`
   (`src/multipath_mapper.cpp:3896`, member declared `src/multipath_mapper.hpp:467-473`),
   after the raw-MEM loop at `3998-4033`, dedup against the existing `hits_already_used`
   set (`3978-3994`). Both `find_spliced_alignments` overloads call this (single-end
   `src/multipath_mapper.cpp:4159`, paired `4469`), so one hook covers both.
2. **Alignment of the seeds (reused unchanged):** `align_to_splice_candidates`
   (`src/multipath_mapper.cpp:3489`) already turns a `(MaximalExactMatch*, pos_t)` hit
   into a dummy single-hit cluster and calls `query_cluster_graphs` + `multipath_align`
   (`3527-3535`). MMP seeds flow through this with no change, provided the synthesized
   `MaximalExactMatch` has valid `begin`/`end` read iterators, `range`, `match_count`,
   and located `nodes` (fields per `find_mems_simple` construction,
   `src/mapper.cpp:102, 208-213`; `mem.hpp`).
3. **Distance pruning + motif scoring (reused unchanged):** `test_splice_candidates`
   (`src/multipath_mapper.cpp:2781`) already prunes donor->acceptor pairs by
   `minimum_distance(*distance_index, ...) > max_intron_length` (`3206-3218`), enumerates
   motif pairs with a budget (`3220-3289`, traced at `3239`), inserts splice edges
   (`3386`), and records the net splice score (`3454`). MMP seeds reuse this verbatim.
   Optionally, `generate_mmp_seeds` can *pre-prune* acceptor loci by `minimum_distance`
   before locating, to collapse high-multiplicity short seeds (the graph analog of STAR's
   window) — an efficiency measure, not a correctness requirement.
4. **Option parsing / config:** add flags in `src/subcommand/mpmap_main.cpp` following
   the trace precedent — `OPT_*` enum near `275-276`, `long_options` near `533-534`,
   `switch` cases near `940-946`, config push near the splicing-parameters block
   `1882-1889`, and a one-time `mpmap_mmp::configure(...)` before the parallel region
   (mirroring `mpmap_trace::open` at `1969-1975`).

## New Flags and Parameters (all hidden/advanced, off by default)

Follow the trace flags' hidden precedent (they appear in `long_options`/cases but not in
`help_mpmap`). All default off / to existing mapper defaults:

- `--mmp-seed` (no-arg): enable the MMP generator. Default false.
- `--mmp-min-prefix N`: minimum MMP length to keep a seed (floor to bound hit counts).
  Default e.g. 12 (below `min_clustering_mem_length` but above pure noise).
- `--mmp-overlap-tol N`: breakpoint overlap tolerance. Default = mapper
  `max_softclip_overlap` (`src/multipath_mapper.hpp:192`, value 8).
- `--mmp-min-intron N` / `--mmp-max-intron N`: distance-prune bounds. Defaults = 20 and
  mapper `max_intron_length` (`src/multipath_mapper.hpp:196`, `1<<18`).
- `--mmp-hit-max N`: per-seed `locate` cap. Default = mapper `hit_max`
  (`src/mapper.hpp:280`).
- `--mmp-strand-mode {native|rc|both}` (advanced): controls right-tail RC querying.
  Default `native` initially; `both` once RC is validated.

Guard: require a distance index when `--mmp-seed` is set (error like other
index-dependent options at `src/subcommand/mpmap_main.cpp:564`), since pruning needs
`minimum_distance`.

## Instrumentation: Reuse and Extend the Trace Layer

The generator runs inside `multipath_map`'s `TraceGuard` scope (rescue block
`src/multipath_mapper.cpp:430-438`), so `mpmap_trace::current()` is valid and metrics
attribute to the current read/anchor with no new plumbing. Extend
`src/mpmap_trace.{hpp,cpp}` (bump `schema_version` to 2 at `src/mpmap_trace.cpp:290`):

- New per-read aggregate fields in `SpliceSearchTrace` (`src/mpmap_trace.hpp:121-200`),
  appended after the existing splice aggregates:
  - `n_mmp_seeds_generated`, `n_mmp_seeds_kept_short` (below the current
    `min_softclip_length_for_splice`, i.e. what the MEM path drops),
  - `n_mmp_junction_partners_recovered` (seeds within intron bounds on a consistent
    strand),
  - `n_mmp_partners_not_in_mem_pool` (MMP seed whose (read-interval, graph-pos) matches
    no existing raw MEM hit — the core recovery delta vs MEM seeding),
  - `n_mmp_short_overhang_recoveries` (breakpoint-pinned seeds shorter than
    `min_softclip_length_for_splice` that became splice partners),
  - `time_mmp_seed_usec` (wrap generation in a `ScopedTimer`, `src/mpmap_trace.hpp:242-260`).
- New nested sub-record `MmpSeedTrace` (sibling to `SpliceAnchorTrace`) with
  `{read_begin, read_end (breakpoint-pinned), match_count, reported_hits,
  min_dist_to_anchor, within_intron_bounds, motif_found, present_in_mem_pool,
  became_splice_partner}`; add `vector<MmpSeedTrace> mmp_seeds;` to the record.
- New emit helpers: `emit_mmp_seed(...)` in `src/mpmap_trace.cpp` (pattern of
  `emit_anchor` at `120-146`) and appended output in `write()` (`175-274`); a
  `trace_push_mmp_seed(...)` file-static helper in `src/multipath_mapper.cpp` (pattern of
  `trace_push_anchor` at `200-225`) called from the gated block.
- The truth-junction machinery (`--trace-truth-junctions`, `open_truth` at
  `src/mpmap_trace.cpp:299-326`; per-MEM detail emission gate `truth_loaded()`) is reused
  as-is; MMP seed positions are emitted in `MmpSeedTrace` for the same external
  graph->reference truth join used by `mem_details`/`final_positions`.

## A/B Validation on the MHC Fixtures

Fixtures present at `/mnt/ssd/lalli/panSC/tests/fixtures/mhc/`:
`reads/mhc_rna_R1.fastq.gz` + `R2`, spliced graph/indices
`sampleA.spliced.{xg,gcsa,gcsa.lcp,dist,pg}`, and annotation `mhc.gtf` /
`mhc.refpath.gtf` (source for a truth-junction TSV in the
`chrom donor acceptor strand junction_id` format expected by `open_truth`,
`src/mpmap_trace.cpp:306-323`). Note: the fixtures' `.dist` may be an older
SnarlDistanceIndex version; regenerate with `vg index -j out.dist graph.xg` if the v1.75.1
binary rejects it.

Design:
1. Derive `mhc.truth_junctions.tsv` from `mhc.refpath.gtf` (external prep script; not
   part of vg).
2. Run `vg mpmap -n rna -x sampleA.spliced.xg -g sampleA.spliced.gcsa
   -d sampleA.spliced.dist -f R2 --trace-splice-search A.jsonl
   --trace-truth-junctions mhc.truth_junctions.tsv` **twice**: once without `--mmp-seed`
   (baseline), once with `--mmp-seed`.
3. **Isolation check:** with `--mmp-seed` absent, the GAM/GAMP output must be
   byte-identical to the pre-change binary (regression guard). With `--mmp-seed` present
   but the generator producing seeds, downstream acceptance may differ; measure the delta.
4. **Recovery metric:** join `mmp_seeds` / `final_positions` to truth externally; report
   per read the number of truth junction partners the MMP path surfaced that the baseline
   MEM pool did not (`n_mmp_partners_not_in_mem_pool`, `n_mmp_short_overhang_recoveries`),
   and whether any became accepted splices (`became_splice_partner`).

**Critical honest caveat (open scoring question):** even when MMP surfaces a short
breakpoint-pinned seed, the downstream candidate re-check in
`align_to_splice_candidates::consider_candidate` still enforces
`min_softclip_length_for_splice` (`src/multipath_mapper.cpp:3549, 3560`), and the anchor
gate enforces `min_softclipped_score_for_splice` (`4106-4107`). So "keep short seeds"
improves candidate **visibility** (fully measurable in the trace, and the honest first
deliverable) but final **acceptance** of very short overhangs additionally needs a
relaxed length/score gate for MMP-sourced candidates — which changes selection and is
explicitly beyond "isolate the seeding change." Treat gate relaxation as a separate,
later, separately-flagged milestone so the A/B cleanly attributes the seeding effect
first.

## Build and Test Plan

- Build with `./build-local.sh` (incremental). Note the documented environment drift and
  workarounds baked into the script: work-dir-specific static protobuf 29.3 + abseil
  toolchain at `/mnt/ssd/lalli/vg-latest-toolchain`, `CC=gcc-13` (elfutils vs gcc-15
  `-Werror`), `CXX=g++-15 CXX_STANDARD=20` (abseil), `--jobserver-style=pipe` (make 4.4),
  and vg's vendored `include/`/`lib` first on the search path (`build-local.sh:22-64`).
  The new `src/mpmap_mmp.cpp` must be added to the object list the Makefile globs (verify
  it is picked up by the existing `src/*.cpp` rule; `mpmap_trace.cpp` already is).
- Extend `test/t/35_vg_mpmap_trace.t` (currently `plan tests 16`): add assertions on the
  spliceable single-end fixture (`trs.fq`) run with `--mmp-seed --trace-splice-search`:
  header `schema_version == 2`; read record `has("mmp_seeds")` and the new aggregate
  fields; on the known spliced read, `n_mmp_seeds_generated >= 1`; and a **default-off**
  assertion that a run without `--mmp-seed` produces a GAMP identical to the current
  behavior (guard against accidental default-path activation).

## Staged Milestones (incremental, independently testable, reversible)

- **M0 — Feasibility spike (done).** GCSA2 backward-only, reset-to-full reseed,
  keep-short confirmed with citations. No code.
- **M1 — Module + flag scaffold.** Add `src/mpmap_mmp.{hpp,cpp}` (gate + config + no-op
  generator returning empty) and `mpmap_main` option plumbing. Test: flags parse; default
  output unchanged; builds clean. Fully reversible (generator no-op).
- **M2 — Left-tail native generator.** Backward-search MMP from `interval.first`,
  reset-to-full reseed at breakpoint, keep-short, `locate` (capped), optional
  `minimum_distance` pre-prune; inject into `hit_candidates_out` behind the gate;
  thread_local `deque` backing store. Test on `trs.fq`: MMP seeds appear; existing splice
  still found.
- **M3 — Right-tail RC generator.** Add reverse-complement query + strand flip
  (`--mmp-strand-mode`). Validate located-position strand empirically (the
  `NEEDS VERIFICATION` item). Test on a right-junction fixture.
- **M4 — Instrumentation.** Extend trace schema (v2), `MmpSeedTrace` + aggregates +
  `trace_push_mmp_seed`; extend `35_vg_mpmap_trace.t`.
- **M5 — Multi-junction iterate + MHC A/B.** Re-seed from the end of each recovered
  segment until the read is consumed; run the A/B; report recovery deltas; tune
  `--mmp-*` defaults.
- **M6 — Paired-end parity.** Wire the same hook through the paired
  `find_spliced_alignments` overload (`src/multipath_mapper.cpp:4303`, hook already shared
  via `identify_unaligned_splice_candidates` at `4469`); verify on the paired fixture.
- **M7 (optional, separately flagged) — Acceptance-gate relaxation** for MMP-sourced
  short overhangs, to convert visibility into accepted splices; measured separately.

## Risks, Unknowns, Open Questions

- **Pointer lifetime** of synthesized `MaximalExactMatch` (must use a pointer-stable
  `thread_local deque`, cleared per read) — highest risk.
- **RC double-strand correctness** (`NEEDS VERIFICATION`): GCSA2 index-build strand
  semantics were not inspected; validate RC-query hit orientation before trusting the
  right-tail path.
- **Hit explosion** for short, high-multiplicity seeds: `--mmp-min-prefix`,
  `--mmp-hit-max`/`hard_hit_max`, and distance pre-pruning are essential; risk of runtime
  blowup on repetitive MHC regions — bound and measure via `time_mmp_seed_usec`.
- **MEMAccelerator applicability**: `accelerate_mem_query` only accelerates from read
  start (`src/mapper.cpp:1899-1914`); arbitrary-offset reseeds simply start from the full
  range — acceptable, just no acceleration.
- **Downstream length/score gates** still filter short overhangs at acceptance
  (`multipath_mapper.cpp:3549, 3560, 4106-4107`) — open question whether/where to relax
  (M7), kept out of the isolated-seeding A/B.
- **Distance index requirement** when `--mmp-seed` is on (enforce, else no pruning).
- **Open design choice**: native-only vs native+RC framing; recommend native-first, add
  RC only if the trace shows right-tail short-overhang partners are being missed.

## Critical Files for Implementation

- `src/multipath_mapper.cpp` (splice driver `find_spliced_alignments` :4042/:4303;
  candidate surfacing `identify_unaligned_splice_candidates` :3896 and hit loop
  :3998-4033; `align_to_splice_candidates` :3489; `test_splice_candidates` distance/motif
  :2781/:3206-3239)
- `src/mapper.cpp` (GCSA2 MMP primitives: `find_mems_simple` :123-215, `find_mems_deep`
  :915-1160, `gcsa->LF` reseed :143/:446/:1024, `accelerate_mem_query` :1899)
- `src/subcommand/mpmap_main.cpp` (flag plumbing :275-276/:533-534/:940-946, splice config
  :1882-1889, one-time init :1969-1975)
- `src/mpmap_trace.hpp` and `src/mpmap_trace.cpp` (trace schema/sink to extend;
  `SpliceSearchTrace` :121-200, `write` :175-274, `TraceGuard`/`current` :226-238/:52-55)
- `test/t/35_vg_mpmap_trace.t` (test to extend) and `build-local.sh` (build workarounds);
  new module to add: `src/mpmap_mmp.{hpp,cpp}`
