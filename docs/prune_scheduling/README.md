# Prune phase profile and unfold scheduling

## What this document is

A per-phase wall-clock profile of one pangenome-scale `vg prune -u` run, and the
scheduling work that profile justifies. It exists because every prior prune
measurement in this fork is an aggregate — a total wall, a peak RSS, an average
CPU% — and aggregates cannot say which phase to attack. `CLAUDE.md`'s
"PhaseUnfolder/prune concurrency" section records a 518% average CPU on chr2 as
evidence that `bbf264574` engages; that average is true and also hides the
distribution described below.

Scope: this is **code-performance** work on `vg prune`. It is not production
indexing and not annotation policy. It authorizes no change while the chr2
production run is live (see *Sequencing*).

## The measurement

Source: the chr2 production prune, stage unit `chr2-preprune-prune-5f82c69e8cac`,
attempt `prune_v4_cap480_20260918T055521Z/attempts/prune-5f82c69e8cac/`. Phase
boundaries were recovered from the stderr byte-offsets recorded in the measured
helper's five-second `samples.jsonl`, so each boundary is the first sample after
the corresponding log line was written (±5 s, not a marked timestamp).

Command (ordinary, current pinned binary `4f495d70…4273c`):

```
vg prune -p -u -k 32 -M 0 -t 24 -g guide.gbwt -a -m chr2.mapping genic.pg > chr2.pruned.pg
```

Input `genic.pg`: 173,581,764,087 bytes (161.66 GiB), 9,520,546 nodes,
13,798,365 edges, 205,672,615 bp of node sequence, carrying 14,952,173
biological transcript paths totalling 592,893,154,207 bp — about 2,882x path
coverage of the graph's own sequence, on the order of 2.7e10 path steps.

| phase | minutes | share | effective cores | ends at log line |
|---|---:|---:|---:|---|
| graph load | 0 – 10.5 | 1% | 1 | `Original graph …` |
| XG path structures, then input path release | 10.5 – 264.9 | 33% | 1 | `Removed all paths` |
| XG reverse index (`np_iv`/`nr_iv`/`nx_iv`) | 264.9 – 381.9 | 15% | 1 | `Built a temporary XG index` |
| prune passes + small-subgraph removal | 381.9 – 382.4 | 0.1% | 1 | `Removed small subgraphs: …` |
| `complement_components` | 382.4 – 544.3 | 21% | 1 | `Complement graph: …` |
| unfold (parallel, decaying) | 544.3 – 762.1 | 29% | 12 → 2 | `Unfolded graph: …` |
| `extend` + serialization | 762.1 – 767.3 | 0.7% | 1 | `Serialized the graph: …` |

**Total 767.3 min = 12 h 47 m 16 s wall, exit 0.** GNU-time maximum RSS
357,638,520 KiB = 341.07 GiB against the 480 GiB cap (71%); measured-helper peak
aggregate RSS 340.826 GiB; zero swap, zero OOM. CPU 92,324 s (89,081 user +
3,243 system) over 46,036.6 s wall = **2.006 effective cores against 24
requested**, which GNU time reports as 200%. Outputs: `chr2.pruned.pg`
6,774,195,898 bytes (50,647,839 nodes, 53,787,207 edges) and `chr2.mapping`
340,858,176 bytes. Receipt:
`prune_v4_cap480_20260918T055521Z/completed/prune.json`.

Percentages are of the 767.3-minute total.

Peak process RSS **340.8 GiB**, of which 336.9 GiB (2.08x the input file, 1.67x the loaded graph),
was set during XG path-structure construction, before the input paths were
released; the unfold added the remaining 3.9 GiB.
Memory then fell to 185.8 GiB as `destroy_all_paths` ran, rose to ~325 GiB as the
reverse index was allocated, and has been flat since. Zero swap, zero OOM events.

Phase-boundary node/edge counts: `Pruned complex regions: 9,520,546 nodes,
10,702,157 edges`; `Removed small subgraphs: 8,040,489 nodes, 10,389,968 edges`;
`Complement graph: 1,862,080 nodes, 3,224,188 edges in 95,026 components`;
`Unfolded graph: 42,989,373 nodes, 43,397,239 edges on 800,834 paths` — the
duplicated subgraph is 5.3x the pruned graph's own 8,040,489 nodes, and is merged
in by `handlealgs::extend` before serialization.

**Two results follow immediately.**

First, the memory question this fork has carried is settled for prune at chr2
scale: 336.9 GiB, against a chr21-anchored prediction of ~350 GiB (the chr21
current-binary prune peaked at 74.815 GiB on a 34.52 GiB `genic.pg`, 2.167x).
The scaling estimate held within 4%. An earlier projection of ~1,300 GiB, derived
from chr19 `vg convert -x` receipts, is withdrawn: those were produced by vg
v1.74.1, whose `XG::index_node_to_path` used a disk-spilling `mmmulti::map` that
the pinned binary does not contain, so most of that "RSS" was evictable mapped
file pages. The withdrawal is recorded in `docs/gbwt_creation/README.md`.

Second, the ranked targets are not where attention had been going:

1. **XG construction — 6.19 h (48%), one core.**
2. **Unfold — 3.63 h (28%)**, nominally parallel but decaying to 2 of 24 threads.
3. **`complement_components` — 2.70 h (21%), one core.**
4. Graph load 1.4%; `extend` + serialization 0.7%; the prune passes that
   `b47de4db9` optimised, 0.1%.

The single number that frames all of it: **2.006 effective cores out of 24 over
12.8 hours**. Roughly 10.5 of those hours were single-threaded — the two XG
sub-phases and `complement_components` — and the nominally parallel unfold
averaged well under its allocation. A perfectly scheduled unfold alone would not
fix the run; it would cut about 3 h from 12.8 h. The serial phases above it are
where the remaining 8.9 h lives.

An interim version of this document, written while the unfold was only 86 minutes
in, ranked the unfold third behind `complement_components`. That was wrong: the
unfold ran 3.63 h and is second. The correction matters because it moves the
scheduling work below from a cheap-but-minor cleanup to the largest tractable
target — XG construction is bigger but is deferred, and `complement_components`
is now smaller than the phase whose load balance is already known to be poor.

## The unfold load-balance finding

`PhaseUnfolder::unfold()` (`src/phase_unfolder.cpp:19`) partitions the complement
into components, then processes them in batches:

```cpp
const size_t batch = std::max<size_t>(1, 8 * static_cast<size_t>(omp_get_max_threads()));
for (size_t start = 0; start < components.size(); start += batch) {
    …
    #pragma omp parallel for schedule(dynamic, 1)      // :52  — parallel unfold
    for (size_t i = 0; i < count; i++) { … }
    for (size_t i = 0; i < count; i++) { … }           // :58  — serial, ordered apply
}
```

At 24 threads that is `batch = 192`, so 95,026 components make **495 batches**,
each with a barrier and a fully serial apply loop during which every thread is
idle.

Observed CPU over the unfold's first hour, as ten-minute averages of cgroup
`cpu.stat`: 12.22 → 8.17 → 4.26 → 3.00 → 3.00 → 2.39 → 2.00 → 2.00 cores. A
10-second per-thread sample then showed **2 threads at 100% and 22 parked on
futexes**.

That signature is batch-level barriers over a skewed component-size
distribution. `schedule(dynamic, 1)` balances *arrival* but cannot un-start a
job: once the small components in a batch drain, the batch sits on its one or
two largest members single-threaded until the barrier releases. Dynamic
scheduling is the right clause and is not sufficient here.

Caveat on the evidence: the decay is inferred from cgroup CPU sampling and one
thread snapshot, not from instrumented per-component timings. The component size
distribution is unmeasured — mean component size is 19.6 nodes, but the tail is
unknown, and the tail is what matters.

## The fix, in order

### 1. Measure first — the floor may be irreducible

Instrument `unfold()` behind an opt-in flag to emit one row per component:
index, `get_node_count()`, `get_edge_count()`, unfold wall-ns, apply wall-ns.
No scheduling change can beat `max(T_largest_component, ΣT / threads)`. If the
largest component is minutes rather than seconds, steps 2 and 3 buy little and
step 4 is the only lever. This measurement also settles whether the serial apply
loop is a meaningful fraction, which decides how much step 3 is worth.

### 2. Longest-processing-time-first ordering within each batch

The component sizes are known before any work starts — `components[i]` is
already a materialised `HashGraph` — but the parallel loop iterates in index
order, which is arbitrary with respect to size. Sorting each batch's iteration
order by descending node count is the textbook LPT makespan heuristic: the
largest component starts at t=0 and the small ones fill in behind it.

**This cannot change the output.** `unfold_component` (`:451`) is pure with
respect to its component — each worker constructs its own `PhaseUnfolder` and its
own `gcsa::NodeMapping` over the same `base`, and it only reads the shared graph
(`graph.has_node(id)` in the border scan). The serial apply loop still runs in
index order, and `apply_component` (`:417`) re-mints duplicate ids in that order,
which is precisely what makes the global numbering independent of scheduling —
the design comment at `:29-41` states this intent. So a reordered parallel loop
yields byte-identical output at every thread count, and the gate can assert that
at T24, which is stronger than the byte-identity gates available elsewhere in
this fork.

Estimated cost: a few lines. Likely most of the available win.

Scale of the prize, now that the phase is complete: the unfold ran 217.9 minutes.
Its CPU trace decayed from 12.22 to 2.00 effective cores within the first hour and
a 10-second thread sample found 2 of 24 busy, so most of those minutes were spent
far below the available 24 cores. What fraction is recoverable depends entirely on
the step-1 distribution — if one component dominates, little; if the tail is a few
hundred mid-sized components spread across batches, most of it.

### 3. Bounded reorder buffer instead of the batch barrier

Replace `for (batch) { parallel-for; serial apply }` with a sliding window:
workers pull the next index from an atomic counter, deposit their ops into slot
`i mod W`, and a single applier consumes slot `i` as soon as it is ready, in
increasing `i`. A worker may not start `j >= lowest_unapplied + W`.

`W` plays exactly the role `batch` plays today — the comment at `:45-47` is
explicit that bounding log residency is the point, because the full set over
every component would not fit at pangenome scale. Apply order is unchanged, so
byte-identity still holds. This removes the 495 drain/refill cycles and overlaps
the serial apply with useful work.

### 4. Intra-component parallelism — only if step 1 demands it

Splitting a single large component's `generate_paths`/`generate_threads` is a
real semantic change and much harder than 2 or 3. Do not open it without the
step-1 distribution showing that the largest component alone dominates.

## `complement_components` — the larger fork-local target

`complement_components` (`:324`) is 2.7 h on one core. It walks every step of
every XG path, tests whether each consecutive-pair edge survives in the pruned
graph, and inserts the misses into one shared `bdsg::HashGraph`. The work is
embarrassingly parallel over paths; the serialisation is the shared graph and
its `has_edge`/`has_node` de-duplication, not the traversal.

Shape of a fix: partition paths across threads, accumulate per-thread edge sets,
merge once. Determinism needs care — the complement's node creation order feeds
component identity and therefore the unfold's apply order — so the merge must be
canonical (e.g. sort by `(from, to)` before insertion) rather than
completion-ordered. This is fork-local code with no upstream coupling, and at
162 minutes it is worth more than the unfold tail.

## XG construction — deferred to its own phase

XG construction is 6.2 h, 59% of the run, entirely single-threaded across two
sub-phases (254 min building path structures, 117 min filling the reverse index)
over ~2.7e10 steps. It is the largest target and it is **explicitly out of scope
here**, by decision on 2026-09-18.

Reasons to keep it separate rather than bolt it on: it lives in `deps/xg`, shared
with upstream, where this fork already carries an uncommitted rewrite of
`index_node_to_path` (retained as
`…/transitions/prune_direct_20260918T045514Z/deps-xg-working-tree.diff`); the
per-step vectors are sized by `bits(2*path_count)`, `bits(max step rank)` and
`bits(max position)`, so any parallel fill has to preserve widths and ordering
exactly; and the same construction is what a standalone `vg index -x` would run,
so the work has consequences beyond prune. It deserves a design doc, a
measurement plan, and its own gates.

One consequence worth recording now: because prune's in-process XG peaked at
336.9 GiB, a standalone `vg index -x` on this graph would plausibly fit the
384 GiB stage cap it was originally assigned, since `VGset::for_each` frees the
input at the same point prune's `destroy_all_paths` fires. That reopens
XG-before-GCSA as a feasible design. It is not a measurement of `vg index -x`,
which has never been measured for this binary at any scale.

## Validation required before any performance claim

- `test/t/38_vg_prune.t`, plus `vg test` unit coverage.
- Byte-identity of **both** the pruned graph and the node mapping at `-t 1`.
  `bbf264574` gated on both; a graph-only gate is insufficient.
- For steps 2 and 3, byte-identity at `-t 24` as well. The design permits it, so
  it should be a hard gate rather than a relabel-invariant digest comparison.
- Separately pinned control and candidate executables and libraries, the same
  frozen input graph and options, varying only the change under test.
- **The controlled A/B that does not yet exist.** `CLAUDE.md` records that
  `prune_benchmarks/chr21_unfold_ab` aborted 5.5 minutes into its serial control
  arm (session teardown, `Linger=no`; lingering is enabled now) and was never
  rerun, so no multiple has ever been attributed to the three merged concurrency
  commits. Any claim about the changes proposed here needs that A/B completed,
  with the step-1 component distribution as the explanation for whatever number
  it produces.

## Sequencing and constraints

Nothing in this document may be implemented while the chr2 production prune is
running. `vg-pinned` resolves `lib/libhandlegraph.so` from this worktree by
RPATH (not RUNPATH — `LD_LIBRARY_PATH` cannot redirect it), and the run pins that
file by hash *and* inode/mtime, re-checked by `check_guards` after the prune
command succeeds but before its outputs are sealed. A relink would therefore
discard a completed multi-hour prune. `.build-freeze` makes `./build-local.sh`
exit 3 until
`…/prune_v4_cap480_20260918T055521Z/PRUNE_COMPLETE.json` exists, and lifts
itself once it does.

## Status of the numbers here

Complete: all phase boundaries through `Complement graph`, the peak RSS, the
node/edge counts, the CPU decay and thread sample.

Complete as of the prune stage's terminal receipt (2026-09-18): all phase
durations, total wall, GNU-time peak, CPU totals, and output identities.

The run is terminal. `prune_check` passed in 1 m 26.89 s at 10.86 GiB:
`vg validate` reported `graph: valid`; `vg stats -z -l -r` gave 50,647,839 nodes,
53,787,207 edges, 264,789,398 bp, node-id range 1:52,127,816; and the structural
mapping check passed with `first_node` 9,520,547, `next_node` 52,127,817 and
42,607,270 unfolded nodes — the mapping extends its immutable seed and no pruned
node id exceeds it. `PRUNE_COMPLETE.json` records `gcsa_executed: false` and
stop `AFTER_PRUNE`.

What this document does **not** establish: any speedup attributable to the three
merged concurrency commits. This is one uncontrolled production run on a shared
host. The A/B named under *Validation* remains unrun.

Everything here is one run, one chromosome, one binary, on a non-exclusive host
shared with unrelated containers. It is a profile, not a controlled experiment.
