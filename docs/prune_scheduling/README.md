# Prune phase profile and unfold scheduling

## What this document is

A per-phase wall-clock and memory profile of one pangenome-scale `vg prune -u`
run, and the scheduling work that profile justifies. It exists because every
prior prune measurement in this fork is an aggregate — a total wall, a peak RSS,
an average CPU% — and aggregates cannot say which phase to attack.

Scope: **code-performance** work on `vg prune`. Not production indexing, not
annotation policy.

## The measurement

Source: the chr2 production prune, stage unit `chr2-preprune-prune-5f82c69e8cac`,
attempt `prune_v4_cap480_20260918T055521Z/attempts/prune-5f82c69e8cac/`. Phase
boundaries come from the stderr byte offsets recorded in the measured helper's
9,187 five-second samples, so each boundary is the first sample after the
corresponding log line (±5 s, not a marked timestamp). Every number below was
recomputed from that trace after the run finished; an earlier draft written
mid-run got two of them wrong, and those errors are called out where they sat.

Command, on pinned binary `4f495d70…4273c`:

```
vg prune -p -u -k 32 -M 0 -t 24 -g guide.gbwt -a -m chr2.mapping genic.pg > chr2.pruned.pg
```

Input `genic.pg`: 173,581,764,087 bytes (161.66 GiB), 9,520,546 nodes,
13,798,365 edges, 205,672,615 bp of node sequence, carrying 14,952,173
biological transcript paths totalling 592,893,154,207 bp — about 2,882x path
coverage of the graph's own sequence, on the order of 2.7e10 path steps
(derived: path bp ÷ 21.6 bp mean node length).

| phase | minutes | share | effective cores | peak RSS (GiB) | RSS at phase end |
|---|---:|---:|---:|---:|---:|
| graph load | 0 – 10.5 | 1.4% | 1 | 203.26 | 203.26 |
| XG path structures, then path release | 10.5 – 264.9 | 33.2% | 1 | 336.89 | 191.25 |
| XG reverse index | 264.9 – 381.9 | 15.2% | 1 | 325.56 | 324.72 |
| prune passes + small-subgraph removal | 381.9 – 382.4 | 0.1% | 1 | 325.45 | 325.45 |
| `complement_components` | 382.4 – 544.3 | 21.1% | 1 | 329.66 | 329.66 |
| unfold | 544.3 – 762.1 | 28.4% | mean 4.51 | **340.83** | 336.38 |
| `extend` + serialization | 762.1 – 767.3 | 0.7% | 1 | 337.67 | 9.95 |

**Total 767.3 min = 12 h 47 m 16 s, exit 0.** Peak RSS 341.07 GiB by GNU time
(357,638,520 KiB), 340.83 GiB by the five-second helper — the two instruments
disagree by 0.07%. Against the 480 GiB cap that is 71% by process RSS; the
cgroup's own peak, which includes page cache, was 393.84 GiB or 82% of the cap.
Zero swap, zero OOM. CPU 92,324 s by cgroup `cpu.stat` (2.006 effective cores of
24 requested) and 92,193 s by GNU time (2.0026); both round to the 200% GNU time
reports. Outputs: `chr2.pruned.pg` 6,774,195,898 bytes (50,647,839 nodes,
53,787,207 edges) and `chr2.mapping` 340,858,176 bytes. Receipt:
`prune_v4_cap480_20260918T055521Z/completed/prune.json`.

Phase-boundary counts: `Pruned complex regions: 9,520,546 nodes, 10,702,157
edges`; `Removed small subgraphs: 8,040,489 nodes, 10,389,968 edges`;
`Complement graph: 1,862,080 nodes, 3,224,188 edges in 95,026 components`;
`Unfolded graph: 42,989,373 nodes, 43,397,239 edges on 800,834 paths` — the
duplicated subgraph is 5.3x the pruned graph's own node count, merged in by
`handlealgs::extend` before serialization.

## Three results

### 1. The unfold owns the peak, not XG construction

Memory has two nearly equal maxima 475 minutes apart. XG path construction
climbs to 336.89 GiB at minute 205.4 and then **releases to 191.25 GiB** when
`destroy_all_paths` fires; the reverse index rebuilds to 324.72 GiB; the global
**340.83 GiB peak falls at minute 680.7, inside the unfold**. An earlier draft
of this document reported these as one event plus a 3.9 GiB increment, which
misidentified the binding constraint. It matters because unfold-side work can
move the run's peak, and XG-side work alone cannot.

The release is not incidental — it is what makes the run fit. The reverse index
is 139.34 GiB (324.72 − 185.38 at its trough). Had it been allocated on top of
the 336.89 GiB path-holding plateau rather than after it, the XG stage alone
would have needed about **476 GiB against a 480 GiB cap**. That figure is
arithmetic on the trace, not a measured counterfactual, but it shows the
`on_input_consumed` hook (`prune_main.cpp:506-508` → `deps/xg/src/xg.cpp:1178`)
was load-bearing for this run rather than an optimization.

### 2. Memory at chr2 scale is settled, and per-graph-byte scaling holds

The run peaked at 341.07 GiB against a chr21-anchored prediction of ~350 GiB
(chr21's current-binary prune peaked at 74.815 GiB on a 34.52 GiB `genic.pg`,
2.167 GiB per GiB of graph). chr2 came in at 2.110 GiB/GiB — **2.6% apart**. The
scaling estimate was good.

An earlier ~1,300 GiB projection, derived from chr19 `vg convert -x` receipts,
is **withdrawn**: those were produced by vg v1.74.1, whose `index_node_to_path`
spilled to a disk-backed `mmmulti` map absent from the pinned binary, so much of
that "RSS" was evictable mapped file pages. Note the narrower claim — mmmulti is
still used for edge sides at `deps/xg/src/xg.cpp:989-993`; what the pinned binary
lacks is the node-to-path spill map specifically.

A separate ~1,192 GiB projection for chr2, carried in
`hprc_v2_vg_rna/notes/chr21_or_vs_exact_dedup_downstream.md`, is also falsified; its
correction is recorded there. Both failed the same way: a ratio measured on one
binary carried forward to a run executed by a later one.

### 3. The ranked targets, and the one number that frames them

1. **XG construction — 6.19 h (48%), one core.**
2. **Unfold — 3.63 h (28%)**, nominally parallel, mean 4.51 of 24 cores.
3. **`complement_components` — 2.70 h (21%), one core.**
4. Graph load 1.4%; `extend` + serialization 0.7%; the prune passes that
   `b47de4db9` optimised, 0.1% — thirty seconds.

**The run used 2.006 effective cores of the 24 requested.** Roughly 8.9 of the
12.8 hours were strictly single-threaded (the two XG sub-phases plus
`complement_components`), and the nominally parallel unfold averaged 4.51. A
perfectly scheduled unfold would not fix the run: its 981.5 core-minutes at 24
cores is 40.9 minutes against 217.7 actual, so the ceiling on that work is about
**3 hours off 12.8**. The serial phases above it hold the rest.

## The unfold load-balance finding

`PhaseUnfolder::unfold()` (`src/phase_unfolder.cpp:19`) partitions the complement
into components and processes them in batches:

```cpp
const size_t batch = std::max<size_t>(1, 8 * static_cast<size_t>(omp_get_max_threads()));
for (size_t start = 0; start < components.size(); start += batch) {
    …
    #pragma omp parallel for schedule(dynamic, 1)      // :52  — parallel unfold
    for (size_t i = 0; i < count; i++) { … }
    for (size_t i = 0; i < count; i++) { … }           // :59  — serial, ordered apply
}
```

At 24 threads `batch = 192`, so 95,026 components make **495 batches**, each with
a barrier and a fully serial apply loop during which every thread idles.

Effective cores per ten-minute window across the whole phase:

```
15.77  5.90  3.54  3.00  2.97  2.00  2.00  2.00  2.00  2.00  2.00
 1.81  1.00  3.06  4.49  5.20  5.14  6.66  7.69  6.96  7.73  6.92
```

The shape is a **U, not a decay**: a fast start, a deep trough of about 100
minutes at or below 3 cores bottoming at 1.0, then recovery to 6–7.7 for the
final 90 minutes. Mean 4.51; 9 of 22 windows at or below 3. An earlier draft
sampled only the first hour and described a monotonic decay to 2 cores — that
was wrong, and the correct shape is more informative: the trough is a bounded
run of expensive components, and parallelism returns once they clear.

That is the signature of batch-level barriers over a skewed component-size
distribution. `schedule(dynamic, 1)` balances *arrival* but cannot un-start a
job: once a batch's small components drain, it sits on its largest members until
the barrier releases.

Caveat: the trace is cgroup CPU sampling, not instrumented per-component
timings. The component size distribution is unmeasured — mean component size is
19.6 nodes, but the tail is what matters and the tail is unknown.

## The fix, in order

### 1. Measure first — the floor may be irreducible

Instrument `unfold()` behind an opt-in flag to emit one row per component:
index, `get_node_count()`, `get_edge_count()`, unfold wall-ns, apply wall-ns.
No scheduling change can beat `max(T_largest_component, ΣT / threads)`, and the
U-shape says a bounded set of components owns the trough. If one component
dominates, steps 2 and 3 buy little. This also settles how much of the phase the
serial apply loop owns, which decides how much step 3 is worth.

### 2. Longest-processing-time-first ordering within each batch

Component sizes are known before any work starts — `components[i]` is already a
materialised `HashGraph` — but the parallel loop iterates in index order, which
is arbitrary with respect to size. Sorting each batch's iteration order by
descending node count is the textbook LPT makespan heuristic.

**This cannot change the output.** `unfold_component` (`:451`) is pure with
respect to its component: each worker builds its own `PhaseUnfolder` and its own
`gcsa::NodeMapping` over the same `base`, and touches the shared graph only
through `graph.has_node(id)` in the border scan, writing solely to
`ops[i]`/`locals[i]`/`paths[i]`. The serial apply at `:59` replays in index order
and `apply_component` (`:417`) re-mints duplicate ids in that order, which is
what makes global numbering independent of scheduling — the design comment at
`:29-40` states the intent. So a reordered parallel loop is byte-identical at any
thread count, and the gate can assert that at T24.

### 3. Bounded reorder buffer instead of the batch barrier

Replace `for (batch) { parallel-for; serial apply }` with a sliding window:
workers pull the next index from an atomic counter, deposit ops in slot
`i mod W`, and a single applier consumes slot `i` in increasing `i`. A worker may
not start `j >= lowest_unapplied + W`. `W` plays the role `batch` plays today —
the comment at `:46-47` is explicit that bounding log residency is the point,
because the full set over 95,026 components would not fit. Apply order is
unchanged, so byte-identity holds. This removes the 495 drain/refill cycles and
overlaps the serial apply with useful work.

### 4. Intra-component parallelism — only if step 1 demands it

Splitting one component's `generate_paths`/`generate_threads` is a real semantic
change. Do not open it without the step-1 distribution.

## `complement_components` — the other single-threaded target

`complement_components` (`:324`) is 2.70 h on one core. It walks every step of
every XG path, tests whether each consecutive-pair edge survives in the pruned
graph, and inserts misses into one shared `bdsg::HashGraph`. The work is
embarrassingly parallel over paths; the serialisation is the shared graph and its
`has_edge`/`has_node` de-duplication, not the traversal.

Shape of a fix: partition paths across threads, accumulate per-thread edge sets,
merge once. Determinism needs care — the complement's node creation order feeds
component identity and therefore the unfold's apply order — so the merge must be
canonical (sort by `(from, to)` before insertion) rather than completion-ordered.
Fork-local code, no upstream coupling. It is smaller than the unfold but its
2.70 h are wholly serial, where the unfold's recoverable share is about 3 h of
its 3.63.

## XG construction — deferred to its own phase

XG construction is 6.19 h, 48% of the run, single-threaded across two sub-phases
(254.4 min building path structures, 117.0 min filling the reverse index) over
~2.7e10 steps. Largest target, and **explicitly out of scope here** by decision
on 2026-09-18.

Reasons to keep it separate: it lives in `deps/xg`, shared with upstream, where
this fork already carries an **uncommitted** rewrite of `index_node_to_path`
(retained as
`…/transitions/prune_direct_20260918T045514Z/deps-xg-working-tree.diff`, verified
SHA256-identical to the live `git -C deps/xg diff`). That uncommitted state is
itself a hazard: a `git checkout` in `deps/xg` would silently change the binary's
memory profile and falsify this document. The per-step vectors are sized by
`bits(2*path_count)`, `bits(max step rank)` and `bits(max position)`, so any
parallel fill must preserve widths and ordering exactly. And the same
construction is what a standalone `vg index -x` runs, so the work has
consequences beyond prune.

One consequence worth recording: prune's in-process XG phase peaked at
336.89 GiB, so a standalone `vg index -x` on this graph would plausibly fit the
384 GiB cap it was originally assigned, since `VGset::for_each` frees the input
at the same point prune's `destroy_all_paths` fires. That is not a measurement of
`vg index -x`, which has never been run at any scale on this binary.

## Validation required before any performance claim

- `test/t/38_vg_prune.t` and the Catch2 unit coverage.
- Byte-identity of **both** the pruned graph and the node mapping at `-t 1`;
  `bbf264574` gated on both, and a graph-only gate is insufficient.
- For steps 2 and 3, byte-identity at `-t 24` as well. The design permits it, so
  it should be a hard gate rather than a relabel-invariant comparison.
- Separately pinned control and candidate executables and libraries, the same
  frozen input graph and options, varying only the change under test.
- **The controlled A/B that does not yet exist.** `prune_benchmarks/chr21_unfold_ab`
  aborted 5.5 minutes into its serial control arm (session teardown under
  `Linger=no`; lingering is enabled now) and was never rerun, so no multiple has
  ever been attributed to the three merged concurrency commits. Any claim about
  the changes proposed here needs that A/B completed, with the step-1 component
  distribution as the explanation for whatever number it produces.

## What this document does not establish

No speedup attributable to any commit. This is one uncontrolled production run,
on one chromosome, on one binary, on a host shared with unrelated containers
throughout. It is a profile, not an experiment.

The 2026-09-12 OR-era chr2 calibration (26.64 GB stripped genic graph, 2:41:33,
194.65 GiB, 96 threads) against this run (173.58 GB, 12:47:16, 341.07 GiB, 24
threads) gives 6.52x the graph for 1.75x the memory and 4.75x the wall. That
comparison carries **four** confounds, not three: thread count, binary, annotation
generation — and the XG rewrite landed between the two runs, so the calibration's
node-to-path index spilled to a page-cache-backed mapped file while this run's is
anonymous `int_vector` storage. The 1.75x memory ratio therefore compares a
partly file-backed peak against a fully anonymous one, which is the same
accounting error that got the ~1,300 GiB projection withdrawn, running in the
opposite direction. Treat it as weak evidence for a scaling law and better
evidence that the width-sizing rewrite works at pangenome scale.

## Status

Terminal. `prune_check` passed in 1 m 26.89 s at 10.86 GiB: `vg validate`
reported `graph: valid`; `vg stats -z -l -r` gave 50,647,839 nodes, 53,787,207
edges, 264,789,398 bp, id range 1:52,127,816; the structural mapping check passed
with `first_node` 9,520,547, `next_node` 52,127,817 and 42,607,270 unfolded
nodes. `PRUNE_COMPLETE.json` records `gcsa_executed: false`.

The RPATH hazard that governed this run is durable and worth repeating: the
pinned `vg` resolves `lib/libhandlegraph.so` from the worktree by RPATH, and the
run pins it by hash *and* inode, re-checked after the stage command succeeds but
before its outputs are sealed. A relink mid-stage discards a completed
multi-hour stage. See "The build freeze" in `CLAUDE.md`.
