# Whole-genome indexing under exact deduplication: what binds, and what does not

Assessment date: 2026-09-20. This assessment combines a read of the prior
measured record and source with a chr21 exact-arm fixture built and measured in
this pass (`exact_dedup_indexing_feasibility/RECEIPTS.md`) and an
adversarially verified survey of GBZ-reachable levers against the source
(`exact_dedup_indexing_feasibility/GBZ_INDEXING_SURVEY.md`). Each figure below
states whether it is measured, projected, or unmeasured, and on which binary.

Binary anchor for every measured figure below:
`4f495d705c5547a39d1334a9c6cd7d4e02ece9e50fe65831ea79ba2679b4273c`, except the
chr21 OR-vs-exact arms, which are the 2026-09-13 binary `35867c7f...`, and the
inherited 3.43x prune-memory-reduction figure under "A fork-code lever"
(256.70 GiB pre-fix versus 74.82 GiB on the 2026-09-16 binary, `545de451...`),
carried forward from `CLAUDE.md` rather than remeasured in this pass; each is
labelled with its own binary where used. Ratios are not carried across
binaries.

## Why this assessment was needed

The goal is a whole-genome `vg mpmap` index built under **exact** transcript
deduplication -- dropping only byte-identical transcripts -- rather than the
intended **AND** rule or the shipped **OR** defect. Exact is the scientific
ideal because in segmental duplications a single base can be the only thing
separating two copies.

What was settled going in: chr2's exact prune is terminal (exit 0, 12:47:16,
341.07 GiB peak under a 480 GiB cap, `prune_check` passed, `gcsa_executed:
false`). What was open: every stage after it. `docs/gbwt_creation/README.md` and
the chr2 status note both record that the XG and distance stages were removed
from the pre-prune plan because `vg prune -u` builds its own XG in-process, and
that they "remain required for mpmap and are still an open design question."
GCSA2 had not started. So the open question was not whether chr2 could be
pruned -- it was which of the remaining stages actually blocks a 23-chromosome
build, and whether that blocker is attributable to exact at all.

## What was checked

Historical read: the OR-vs-exact chr21 comparison and the chr2 terminal
receipts for stage costs; the banked per-chromosome corpus for the
whole-genome denominator; the joint index driver
(`scripts/whole_genome/build_joint_genic_k32_index.sh`) for what the joint
stages assume; and `src/subcommand/mpmap_main.cpp` and
`src/subcommand/prune_main.cpp` for what the mapper actually requires and what
prune actually builds.

New measurement, 2026-09-20: a chr21 exact-arm fixture (`genic.pg`,
37,068,417,249 bytes; `guide.gbwt`, 611,729,792 bytes; 5,607,688 named paths)
was built into a GBZ and an r-index and probed for name preservation, thread
multiplicity, query cost, XG-versus-GBZ size and wall/peak, mpmap loadability,
and end-to-end mapping; full receipts, including probe sources, are in
`exact_dedup_indexing_feasibility/RECEIPTS.md`. A separate adversarially
verified survey then ranked every GBZ-reachable lever against the
source and settled several claims the fixture alone could not; that is
`exact_dedup_indexing_feasibility/GBZ_INDEXING_SURVEY.md`.

## Two graphs, and what GBZ changes

Two different graphs are at stake in every figure below, and conflating them is
the error this section exists to prevent. The **counting graph** is what
`vg rna` builds and `vg prune` unfolds: every exact-deduplicated
haplotype-specific transcript (HST) walk embedded as a path, because
panCollapse's quantification reads compatibility directly off those paths. The
**alignment graph** is what `vg mpmap`'s `-x` needs to place a read and find its
splice junctions, which `hprc_v2_vg_rna/recipe_gbz_mpmap_annotation.md` step 3
already specified as junction structure only -- "For mpmap ALIGNMENT, omit
`-a`/`-r` -- you only need the junctions" -- before this session's measurements
existed. That recipe was right. Every whole-genome projection under "The corpus
denominator" and "The global XG" below is a counting-graph figure; whether the
alignment graph needs to carry it is worked out under "The wall is a design
decision, not a physical limit."

The second conflation to guard against: **a GBZ removes neither `vg prune` nor
GCSA2.** `vg mpmap` hard-exits without `-g` (`mpmap_main.cpp:1633-1635`), and
all three seeders -- `find_mems_deep`, `find_fanout_mems`, `find_stripped_matches`
(`src/multipath_mapper.cpp:707-723`) -- and the splice scorer (`:3801`) are
GCSA2 queries. GCSA2 also syntactically accepts a GBZ as direct input
(`vg index -g out.gcsa -x graph.gbz`), but that is not a substitute for what
prune's passes do: a GBZ's edges are only those its threads support, and the
measured reduction from that alone is 0.926% (below), nowhere near what prune
removes. Prune's complexity-bounding work stays required regardless of which
graph format feeds GCSA2.

What GBZ permission does reach is four stages: the strip stage (dropping
`vg rna -r/--add-ref-paths` empties the path payload from prune's in-process
XG, verified only on a 37-node fixture, not at production scale), the
guide-GBWT stage (`vg rna -b -g` in place of a separate `vg gbwt` insertion
pass, whose net wall-clock change is unmeasured and could be negative because
the stage it replaces used a private multi-worker binary, whose threading
`Transcriptome::add_transcripts_to_gbwt`, a serial `insert` loop, does not
share), the XG (measured below),
and the distance index (measured under "The GBZ as mpmap's graph index").
Ranked by measured saving, with the fixture-scale caveats and the settled
negatives -- including that swapping the XG `PhaseUnfolder` consumes for a GBZ
is dead on query cost, and that `vg autoindex -w mpmap` given a GBZ input
silently plans prune *without* unfolding, an annotation-policy change rather
than an indexing win -- in full at
`exact_dedup_indexing_feasibility/GBZ_INDEXING_SURVEY.md`. This document folds
in that survey's results at the points below rather than restating them.

## The corpus denominator

All 23 chromosomes are banked as `transcript_full.pg` under the OR rule,
totalling 285.68 GB -- the counting-graph corpus defined above, not the smaller
alignment graph mpmap needs. The OR-to-exact graph inflation has two
independent measured points:

| chromosome | OR graph | exact graph | inflation |
|---|---:|---:|---:|
| chr21 | 5.28 GB | 38.15 GB | 7.225x |
| chr2 | 23.84 GB | 178.16 GB | 7.473x |

The two agree to 3.4%. Their mean, 7.349x, projects the exact whole-genome
corpus at **about 2.10 TB**, against 285.68 GB under OR. This is a projection
from two points, not a fitted model, and chr1 -- the largest chromosome -- is
not one of them.

## The cost partition, which decides everything downstream

The durable structural result of the chr21 comparison is that every stage falls
into one of two groups. Stages that track **paths** inflate with the dedup rule;
stages that track **nodes** do not. On chr21, exact carried 5.35x more paths but
produced only 1.145x the nodes and 1.225x the edges:

- Path-tracking: `vg rna` wall 5.61x and RSS 6.98x, graph file 7.21x, guide
  GBWT 4.53x, prune peak RSS 6.12x.
- Node-tracking: pruned graph 1.15x, GCSA2 wall 1.13x, GCSA2 peak RSS 1.06x,
  `.gcsa`+`.lcp` 1.11x.

`prune -u` unfolds haplotype walks, so its working set is set by path count.
GCSA2 consumes the pruned graph, a node-space object, so it is nearly
indifferent to the rule. **This means whole-genome GCSA2 feasibility is
essentially independent of the dedup choice, and should not be charged to
exact.** It is the largest unmeasured stage under *either* rule.

## Stage by stage, at whole-genome scale

### Prune memory is settled and is not the blocker

Prune peak RSS scales tightly with stripped graph size: chr21 at 2.167 GiB per
GiB of graph, chr2 at 2.110 -- 2.6% apart on the same binary. chr2's measured
341.07 GiB came from a 161.66 GiB stripped graph.

Applying 2.110 to chr1's projected exact graph (26.04 GB banked x 7.349 =
191.4 GB = 178.2 GiB) gives a projected prune peak of **about 376 GiB**. The
host has 1,007 GiB. Every per-chromosome stage fits serially with large margin.
Memory binds *concurrency*, not feasibility: two chromosomes of chr1/chr2 scale
would want roughly 717 GiB, so large chromosomes must not prune together.

### Prune wall clock is the schedule cost, and it is mostly serial

If prune wall scales linearly in graph bytes from chr2's 12:47:16 over
161.66 GiB, the 1,955 GiB exact corpus projects to **about 155 hours, or 6.4
days, of serial prune**. This is the weakest projection here -- it is anchored
on two chromosomes and prune is not compute-bound in a way that guarantees
linearity.

What makes it expensive is known precisely from the chr2 phase profile: XG
construction takes 6.19 h (48%) on one core, `complement_components` 2.70 h
(21%) single-threaded, and the nominally parallel unfold 3.63 h (28%) averaging
4.51 of 24 cores. The run used **2.006 effective cores of the 24 requested**,
with roughly 8.9 of its 12.8 hours strictly single-threaded. A perfectly
scheduled unfold would recover at most about 3 h of 12.8; the serial phases
above it hold the rest.

### GCSA2 is unmeasured at production scale, but is not an exact problem

chr21's exact arm finished GCSA2 in 1:46:02 at 25.5 GiB, against the OR arm's
1:33:59 at 24.0 GiB -- 1.13x wall and 1.06x memory. Node-space, as predicted.

One record needs correcting to avoid a false alarm. The joint driver's header
states that "chr2 GCSA2 has never completed at any cap from 25G to 300G." That
sentence describes the **whole-pangenome** scope -- the fall-through path in
the older sharded script that runs `vg rna` with no `-d` and no retention
stream. Making that path unreachable is the stated reason the genic driver is a
separate file. It is not a report of a failure in genic scope, which is the
scope this project builds. Genic chr2 GCSA2 has not been attempted, and chr2's
pruned graph (6,774,195,898 bytes, 50,647,839 nodes) is sitting terminal and
ready for it.

### The global XG is the one place exact plausibly makes the build impossible

The joint driver projected a global XG at 1.427x the graph corpus on disk, from
an OR-rule graph, and noted that sdsl structures are roughly disk-equals-RAM. It
also stated that **no RSS measurement for `vg index -x` existed at any scale in
this workspace**; the 2026-09-12 calibration that superseded the old "~22-day
prune" forecast measured *prune*, not XG construction.

**That measurement now exists.** `vg index -x -t 24` on chr21's exact
`genic.pg`, pinned binary, ran **54:28.44 at 68.52 GiB peak RSS**, exit 0, and
produced a 55,013,545,597-byte XG. The measured `xg/pg` ratio is **1.484**,
4% above the driver's assumed 1.427 -- so the driver's figure was sound.

Applied to the 2.10 TB exact corpus, a global XG projects to **3.12 TB, or 2.88x
the host's 1,007 GiB**, with roughly 51 h of construction. Under the OR corpus
the same arithmetic fits comfortably. This is the one stage where the exact rule
turns a feasible build into an infeasible one, and it is now infeasible on a
measured ratio rather than a borrowed one.

That 3.12 TB is the **counting-graph** XG -- `genic.pg` carries the full exact
transcript-path set as embedded paths. Whether mpmap's alignment graph needs to
carry the same set is worked out next.

## The wall is a design decision, not a physical limit

The 3.12 TB counting-graph figure above assumes the mpmap alignment graph
embeds the full exact transcript path set. As "Two graphs, and what GBZ
changes" states up front, the source says it does not have to.

`vg mpmap` hard-requires exactly two inputs: a graph via `-x`
(`mpmap_main.cpp:1629-1631`) and a GCSA2 index via `-g` (`:1633-1635`). The
distance index `-d` is required only by the minimum-distance and
target-value-search clusterers (`:1569-1574`), not by default operation.

More decisively, at `:1817-1828` mpmap explicitly handles a graph whose
`get_path_count()` is zero. It does not refuse; it warns that path-based pair
rescue will be unavailable. The positional structure it then builds is a
`bdsg::ReferencePathOverlayHelper` (`:1830-1831`) -- an overlay over **reference**
paths. Nothing in the mapper requires 2.38M-per-chromosome exact transcript
walks to be embedded in `-x`.

Since exact inflates graph *structure* by only 1.145x nodes and 1.225x edges, a
graph carrying reference paths plus splice-junction structure is **nearly
dedup-indifferent**, so the 3.12 TB projection should collapse toward the
OR-scale figure. This is an inference from the node/edge inflation ratios, not
a separate measurement: no XG has been built from a reference-paths-only graph
at any scale, so no alignment-graph XG size is measured, only argued. The
transcript walks then live in a GBWT or GBZ, which is where they are
cheap: chr2's guide GBWT is 3.24 GB against a 173.58 GB graph, a 53x
compression. The joint driver already names an "exact-walk GBWT" as a separate
deliverable, so this split is contemplated by the existing design rather than
new.

## A fork-code lever, and the precondition that governs it

`src/subcommand/prune_main.cpp:441` constructs an `xg::XG` in-process, populates
it at `:506-508` via `from_path_handle_graph`, hands it to `PhaseUnfolder` at
`:542` and `:566`, and then discards it. There is no option to supply a prebuilt
XG or to keep the one it builds. That construction is the 6.19 h single-core
phase -- 48% of the chr2 run.

It is tempting to read that as a free win: build it once, serialize it, and hand
it to mpmap. It is not, and the reason matters. Prune is fed the **stripped**
graph, whose embedded paths are biological only, and its XG is built from those
remaining non-alt paths -- which at exact scale is roughly 15M biological
transcript paths per chr2-sized chromosome (chr2's guide GBWT carries
29,902,979 raw embedded paths in total, `docs/gbwt_creation/README.md`;
consistent with applying chr21's retention-pad fraction -- 2,803,513 of
5,607,688, near half -- to chr2's total; the chr2 biological fraction is not
separately measured). That is the 336.89 GiB path plateau.
So the object prune discards is precisely the transcript-path-embedding XG that
the 3.12 TB whole-genome counting-graph projection says is unaffordable.

**The precondition therefore flips.** Retaining prune's XG pays only under the
contract where mpmap's `-x` embeds transcript paths -- the contract this document
argues is the one that makes the whole-genome build infeasible. If the
reference-paths-only contract is adopted instead, prune's internal XG is not the
mpmap artifact, and this lever reduces to a pure prune speedup whose value is the
6.19 h per chromosome, with the mpmap XG still owed and built separately from a
reference-path graph.

A different substitution was also checked, and it is dead: swapping the XG
`PhaseUnfolder` consumes for a GBZ built from the same graph. `PhaseUnfolder`'s
constructor signature invites it, but the query it would need --
`GBWTGraph::for_each_step_on_handle` -- measured **325,873 us per node** on the
chr21 exact GBZ against 0.873 us for a count-only probe (`GBZ_INDEXING_SURVEY.md`,
"Refutations worth knowing"). The lever above is the only one available at this
stage.

Either way, note the memory interaction before acting: the `on_input_consumed`
callback at `deps/xg/src/xg.cpp:1178` destroys the input graph's paths as the XG
copies them, so the graph and the reverse index never coexist. That is the
mechanism behind the 3.43x memory reduction on chr21's exact prune peak RSS
(256.70 GiB on the pre-fix binary against 74.82 GiB on the 2026-09-16 binary,
same input, guide, thread count and cores; `CLAUDE.md`, "This memory work is
the enabling condition..."). Retaining the XG for serialization must not
resurrect the plateau it was written to avoid.

## Can the transcript paths live in a GBWT? What panCollapse requires

The consumer that decides the `-x` path contract is panCollapse, which converts
`vg mpmap` GAMP into alevin-fry RAD records. Its README states it reads
compatibility "directly off the haplotype-specific transcript (HST) paths that
`vg rna` embedded in the graph," and its input/output contract requires a graph
exposing the HST paths (`<transcript_id>_H<n>` / `_R<n>`) and states that "GBZ,
and GBWT are not panCollapse inputs."

That exclusion is a scoping decision with a stated acceptance condition, not a
technical refutation. panCollapse's own `docs/decisions.md` (a separate
project, not part of this repository) records: "GBZ/GBWT is a future
coordinate-index alternative, not a V1 replacement for `.xg`, unless later proven
to expose equivalent `PathPositionHandleGraph` behavior and pass the same
projection fixtures." So the question is answerable on its own terms, and the
acceptance test is already written down.

The answer splits into two queries, and they behave oppositely.

### The node-to-transcript-set query: served natively, and compressed

`GBWTGraph` is declared `public PathHandleGraph`
(`deps/gbwtgraph/include/gbwtgraph/gbwtgraph.h:52`) and implements
`for_each_step_on_handle_impl` (`deps/gbwtgraph/src/gbwtgraph.cpp:1102`), which
delegates to `for_each_edge_and_path_on_handle` -- enumerating every GBWT thread
visiting the node, not merely named or reference paths. Two details that could
have made this useless do not: `get_path_count()` returns
`index->metadata.paths()` (`:700-703`), the count of all threads rather than
named paths, so the early return in `for_each_step_on_handle_impl` does not fire
on a haplotype-only GBWT; and `get_sense()` returns `PathSense::HAPLOTYPE` for
any handle beyond the named-path cache (`:1120-1134`), so HST threads are
addressable as paths.

This is the GBWT's native strength rather than a workaround. A GBWT is a
node-keyed, run-length-compressed index of path visits, and exact deduplication
produces precisely the input that compresses best -- millions of *near-identical*
transcript walks. The same property that makes exact expensive in XG makes it
cheap in GBWT. Two measured points support this: chr2's guide GBWT holds
29,902,979 transcript paths (`docs/gbwt_creation/README.md`) in 3.24 GB against
the 173.58 GB graph carrying the same paths, and in the chr21 arms the guide
GBWT inflated 4.53x under exact where the
graph file inflated 7.21x. The GBWT already absorbs the exact rule better than
the graph does.

### The step-to-position query: not served, and this is where the cost hides

panCollapse does not only ask which transcripts touch a node. Its projection
algorithm uses `for_each_step_position_on_handle` and `get_position_of_step` to
convert node-local spans into transcript-coordinate intervals and to build splice
keys. That is the `PathPositionHandleGraph` interface. `XG` implements it
(`deps/xg/src/xg.hpp:181`). `GBWTGraph` does not -- it declares only
`PathHandleGraph`.

The obvious bridge is `bdsg::PackedPositionOverlay`, which wraps any
`PathHandleGraph` into a `PathPositionHandleGraph`. **It does not solve the
problem, and reading what it stores is what shows why.** Its per-path `PathIndex`
(`deps/libbdsg/bdsg/include/bdsg/overlays/packed_path_position_overlay.hpp:292-309`)
holds `steps_0` and `steps_1`, a `positions` vector, a boomphf minimal perfect
hash over the step handles, and a `step_positions` vector. That is
O(total steps across every indexed path) -- the same complexity class as XG's
`np_iv`, differing in constant factor because the vectors are bit-packed rather
than in how they scale. Constructing it with `all_paths` set, which is what
haplotype-sense HST threads would require, re-materializes the plateau the GBWT
was chosen to avoid.

So the route is not "GBZ plus a position overlay."

### The shape a working answer would have

The hypothesis worth testing was that the position index is unnecessary here.
XG's positional machinery exists because reference paths are chromosome-length:
you cannot walk to an offset from the path start. An HST path is one transcript,
so walking its thread from the beginning ought to be bounded by transcript length
rather than chromosome length.

**The fixture refuted this.** Measured mean walk length to reach a queried step
is 1,645.12 hops, at 266.85 us per position query. The cause is node
granularity, not transcript length: the chr21 exact graph holds 34,459,835 bp in
2,056,621 nodes, a mean node of 16.8 bp, so a transcript of ordinary length still
spans thousands of graph nodes. Walking is not a cheap substitute for
`get_position_of_step`, and the argument from path length does not survive
contact with a pangenome graph's node sizes.

### The cost that moves, and the threshold that judges it

`get_path_handle_of_step` resolves a step to a path identity through
`index->locate()` (`deps/gbwtgraph/src/gbwtgraph.cpp:869-879`). XG answers the
same question with a direct `np_iv` lookup. So the per-query cost moves from an
array read to a GBWT locate, whose cost is set by the document-array sample
interval -- a tunable whose defaults are chosen for haplotype panels, not for
millions of short transcripts, and which may need to be denser here.

panCollapse has already written the acceptance thresholds for this, and names
"high path multiplicity for aligned nodes" as its main risk in its own
`docs/research/annotation-lookup-performance.md` (a separate project, not part
of this repository): revisit the lookup index if
annotation lookup exceeds 50% of wall or CPU time, median lookup exceeds 50 us
per read group, p95 exceeds 500 us, or p95 projected path occurrences per aligned
node exceeds 500. Exact deduplication raises per-node path multiplicity by
roughly the retention ratio, so it pushes directly against the fourth threshold.
These numbers, not a general argument about compression, are what should decide
the question.

### Measured on chr21, 2026-09-20

The fixture was built and run on the pinned binary. Full receipts, including
probe sources, are at
`exact_dedup_indexing_feasibility/RECEIPTS.md`, with the probe sources and
build lines beside them in that directory.

**Names round-trip exactly.** `vg gbwt -M` reports 5,607,688 paths with names,
231 samples, 461 haplotypes. The 5,607,688 names emitted by `vg gbwt -T` are
identical as a sorted set to the arm's own XG-side `guide.names`. All carry the
`_R<n>` suffix; none is stripped. panCollapse's opacity requirement is met.

**The GBZ is 59.5x smaller than the PackedGraph.** `vg gbwt -x genic.pg -g
chr21.gbz guide.gbwt` ran 3:30.59 at 46.35 GiB peak RSS, exit 0.

| artifact | bytes |
|---|---:|
| `genic.pg`, PackedGraph with paths embedded | 37,068,417,249 |
| `guide.gbwt` | 611,729,792 |
| `chr21.gbz` | 622,882,936 |

The GBZ holds the same graph and the same 5.6M transcript walks, and exceeds the
GBWT alone by 11,153,144 bytes -- the entire node-sequence payload for
34,459,835 bp. This is the path-versus-node partition measured directly: the
walks cost essentially nothing once they are in a GBWT rather than embedded.

**Per-node thread multiplicity is 15x over panCollapse's own threshold.** Over
50,000 uniformly sampled nodes: mean 2,166.62, median 344, **p95 7,453**, max
136,523. panCollapse's stated bottleneck threshold is p95 above 500, defined
over aligned nodes; this sample is uniform over all graph nodes,
which a read-mapping workload would not visit uniformly, so the two p95s are
not measuring the same population even though both describe chr21, the
smallest chromosome.

**Composing the constraint along a read is cheap but does not reduce the
answer.** `find` plus `extend` costs 0.873 us per node, and a 24-hop walk with
branch evaluation at every hop costs 14.54 us over a mean 517.9 bp. But across
20,000 such walks the median surviving candidate set fell only from 345 to 233,
and the mean from 2,155 to 1,467 -- **68% of candidates survive a read-length
extension**. At the 87,586 branching events encountered, minority branches
carried a mean 8.4% of threads. Junction-style discrimination, in this form, is
not available.

**Query cost is where the GBZ loses, badly.** The storage win does not carry
into the lookup. Measured over 5,000 nodes and 2,000 steps:

| operation | cost |
|---|---:|
| `for_each_step_on_handle`, per node | **325,873 us (325.9 ms)** |
| same, per step | 156.62 us |
| `locate()` per step (no r-index) | 84.68 us |
| position by walking the thread | 266.85 us, mean 1,645.12 hops |
| `find` + `extend` (count only, no enumeration) | 0.873 us |

panCollapse's threshold is a median lookup under 50 us per read group. Enumerating
one node's threads costs 325.9 ms -- roughly **6,500x** over it. XG answers the
same question with a direct `np_iv` read. So a GBZ is not a drop-in replacement
for the XG on this query: it trades a 59.5x storage win for a query that is
orders of magnitude slower, because every step resolution is a GBWT `locate()`
rather than an array lookup.

The one operation that stays fast is the one that never enumerates: composing a
SearchState costs 0.873 us per node and yields the candidate *count* without
identities. Counting is cheap; naming is expensive.

**The reason is the useful finding.** 2,166.62 divided by 792 haplotypes is
2.736: the candidate set is ~792 haplotype-specific copies of ~2.7 overlapping
transcript models. (This 792 has not been reconciled against the 461
haplotypes `vg gbwt -M` reports for this guide GBWT, above; see "What this
assessment does not establish.") Exact deduplication is the rule that
deliberately preserves those copies, which differ only at the variant sites
that make them non-byte-identical. A read not overlapping a distinguishing
variant cannot separate them by any index -- the redundancy is information,
not an indexing defect.

For gene-level counting, however, the haplotype factor is not signal. Collapsing
the answer to distinct transcript models or genes *inside* the query takes
effective multiplicity from ~2,167 to ~2.7, from 15x above panCollapse's
threshold to two orders of magnitude below it. That is a change to what the
query returns -- model or gene identifiers rather than per-haplotype path
identifiers -- not a change to the graph or the index structure.

## The GBZ as mpmap's graph index

`vg mpmap` hard-requires only `-x` (`mpmap_main.cpp:1629-1631`) and `-g`
(`:1633-1635`), and it loads `-x` polymorphically through
`vg::io::VPKG::load_one<PathHandleGraph>` (`:1758`). GBZ is registered with that
registry (`src/io/register_loader_saver_gbz.cpp`), and `GBWTGraph` is a
`PathHandleGraph`, so a GBZ is loadable as mpmap's graph. Confirmed empirically:
mpmap loaded `chr21.gbz` without error, proceeded to the GCSA2, and -- as item 3
below now shows -- on to a mapped read. **None of this removes `vg prune` or
GCSA2**; both stay on the critical path regardless of which artifact fills
`-x` ("Two graphs, and what GBZ changes," above).

On the same chr21 input, the two candidate `-x` artifacts measure:

| | `vg index -x` | `vg gbwt -g` (GBZ) | ratio |
|---|---:|---:|---:|
| wall | 54:28.44 | 3:30.59 | 15.5x faster |
| peak RSS | 68.52 GiB | 46.35 GiB | 1.48x less |
| artifact | 55,013,545,597 B | 622,882,936 B | **88.3x smaller** |

Projected to the whole genome, the XG is 3.12 TB against a 1,007 GiB host and
the GBZ is on the order of tens of GB (a single-input projection; see "What
this assessment does not establish"). **The GBZ is the only candidate measured
to fit**, which makes it relevant to feasibility and not only to panCollapse --
with one dependency this document had not checked until the survey pass:
**`rpvg`, the downstream quantifier named in the prepared `joint_mpmap_rpvg_*`
runs, requires an XG input** ("Graph (xg format) input required", rpvg
`src/main.cpp:437`). If rpvg stays in the pipeline, the XG is deferred, not
eliminated, and this saving must not be booked against the whole-genome budget
until that dependency is resolved.

**The GBZ is not merely smaller than the XG; it is topologically different.**
`vg stats -N -E` gives `chr21.gbz` 2,056,621 nodes (identical to the source) and
2,701,234 edges against the source's 2,726,485 -- **25,251 edges, 0.926%,
dropped**, because a `GBWTGraph`'s edge set is only what the GBWT's threads
support. For mapping to the annotated transcriptome, edges no transcript
crosses are arguably dispensable. For this fork's stated purpose --
`vg mpmap --trace-splice-search`'s search for evidence of *novel* splice
junctions -- an edge crossed by no *annotated* transcript is exactly the object
of interest, and building the mapping index from an annotation-derived GBWT
removes it before the search begins. Neither what those 25,251 edges are nor
whether GCSA2 indexes k-mers crossing them has been measured.

Four things are required before calling it an mpmap index, and two of the four
are no longer open.

1. **The `reference_samples` tag was the lever tried first; naming is the lever
   that actually matters.** mpmap applies a `bdsg::ReferencePathOverlayHelper`
   (`:1830-1831`), which instantiates `ReferencePathOverlay`
   (`deps/libbdsg/bdsg/include/bdsg/overlays/overlay_helper.hpp:35`), and
   `ReferencePathOverlay` indexes REFERENCE **and** GENERIC sense paths by
   default (`deps/libbdsg/bdsg/src/reference_path_overlay.cpp:31-32`). `vg gbwt
   -Z --tags` on the GBZ built plainly from `guide.gbwt` shows
   `reference_samples` listing all 230 HPRC samples (CHM13 is not a sample in
   this guide GBWT at all, so it cannot appear either way) -- so essentially
   all 5,607,688 HST paths are REFERENCE- or GENERIC-sense (1,398,636 and
   4,209,052 respectively, below), and mpmap builds a position index over all
   of them: the O(total steps) structure
   this document identified earlier.

   Measured consequence: `vg mpmap -x chr21.gbz -g chr21.gcsa` on **five reads**
   grew 2.07 -> 36 -> 95.71 -> 147.09 GiB over 11 minutes and was still climbing
   when stopped. `vg sim -x chr21.gbz` reached 221 GiB on the same file. Neither
   figure is a mapping cost; both are the overlay.

   Rebuilding with `vg gbwt -x genic.pg --set-reference CHM13 -g chr21.ref.gbz`
   (2:07.91, 46.24 GiB, 622,881,552 bytes) yields `reference_samples CHM13`.
   **This barely helped, and an earlier version of this section overstated
   it.** `ReferencePathOverlay` indexes REFERENCE *and* GENERIC, and
   `get_sample_sense` (`deps/gbwtgraph/src/utils.cpp:174-188`) assigns GENERIC
   to any path whose name carries no PanSN sample field -- a rule
   `reference_samples` cannot override. Counted directly on the mapped GBZ:

   | | REFERENCE | GENERIC | indexed by the overlay |
   |---|---:|---:|---:|
   | `chr21.gbz` | 1,398,636 | 4,209,052 | 5,607,688 |
   | `chr21.ref.gbz` | 0 | 4,209,052 | 4,209,052 |

   The tag cut the overlay's input by only 25% (the 2,803,513 retention-pad
   walks plus 1,405,539 non-PanSN names stay GENERIC regardless of the tag),
   and the two mpmap runs tracked accordingly -- 38 vs 36 GiB at 8 minutes, 102
   vs 95.7 GiB at 10. **A GBZ 88.3x smaller than the XG can still cost more
   resident memory than the XG if every path in it is reference- or
   generic-sense.** No cheap fix exists: see "The overlay has no cheap lever" below.
   A naming change at the guide-GBWT stage
   so only the chromosome reference is PanSN-qualified as reference or
   generic and every transcript walk resolves to HAPLOTYPE -- not a
   `--set-reference` tag change. No shipped `vg` flag reaches that naming
   change today.
2. **A distance index -- now measured, not merely warned for.** mpmap calls one
   "HIGHLY recommended" on splice graphs. Built from the GBZ: **2:31.32 at
   8.06 GiB**. Built from the XG on the same chr21 input: **3:45.47 at
   60.59 GiB** -- **7.5x less peak RSS, 1.49x less wall**, at 5.2x the
   CPU-seconds (1,010.6 vs 195.6, i.e. GBZ load time, not parallel
   construction). One run per arm on a loaded host is not a noise floor, but
   the memory ratio is far outside plausible contention while the wall ratio is
   not, so hold the two to different confidence. `fill_in_distance_index` takes
   a bare `const HandleGraph*` (`src/snarl_distance_index.hpp:34`); the distance
   index never needed an XG. Use the positional form -- `-x` is hard-typed to
   `xg::XG` (`src/subcommand/index_main.cpp:778`), while the GBZ path is the
   positional branch (`:790-806`). **No mapper has yet opened either distance
   index built here**, and the mapped run in item 3 had no `-d`.
3. **Mapped output -- answered on a smoke test.** `vg mpmap -x chr21.ref.gbz -g
   chr21.gcsa -f reads.fq -t 4` (no `-d`) mapped **5/5** 150 bp reads at
   **MAPQ 60** with non-empty subpaths, two of the five showing genuine
   multipath structure (multiple start subpaths). 27:29.23 wall, 154.55 GiB
   peak RSS, exit 0. This establishes that a GBZ is loadable and usable as
   `vg mpmap -x` and produces valid multipath alignments; it does **not**
   establish accuracy, throughput, or behaviour at scale, and the wall and peak
   are dominated by the overlay and the missing-distance-index fallback below,
   not by mapping five reads.
4. **`ref_path_handles` has two distinct failure modes, both silent.** It does
   not consult path sense: for each connected component it inserts the single
   longest path (`mpmap_main.cpp:1845-1855`), so on a graph whose only paths
   are transcripts and retention pads, mpmap adopts the longest transcript per
   component as that component's splice-alignment "reference" -- exit 0, no
   warning, no MAPQ collapse visible in the five-read smoke test, but
   unevaluated at scale. The second mode is worse: a GBZ built via `vg rna -b`
   (the guide-GBWT-stage replacement named above) has *no* named paths, because
   `cache_named_paths` returns early without contig names
   (`deps/gbwtgraph/src/gbwtgraph.cpp:479-484`), which empties `ref_path_handles`
   entirely (`mpmap_main.cpp:1835-1857`) instead of choosing badly. The survey's
   adversarial pass reproduced this against a known prior hazard: exit 0, no
   warning, MAPQ 1 -> 60, secondary alignment dropped. **Any route to `-x` GBZ
   must be gated on an explicit path-sense check before mapping, never inferred
   from exit status.**

**Startup cost, measured end to end.** The mapped run's 27.2-minute startup
splits into two independently avoidable costs, timed on the untagged
`chr21.gbz`: `overlay_helper.apply()` (`:1830-1831`) took **14.3 of 27.2
minutes** (3.3 -> 17.6 m) -- the larger cost, and an earlier revision of this
document claimed the overlay was *not* the mechanism, which was wrong and is
retracted here. The component-labeling pass (3.5 min, 19.0 -> 22.5 m) is
separately avoidable: it is gated on `distance_index_name.empty() &&
path_handle_graph->get_path_count() > 0` (`:2013-2016`), so supplying `-d`
removes it -- but that run has not yet been made.

## A candidate path for panCollapse counting under the exact rule

This is the route the chr21 fixture supports. It is a plan, not an accepted
result: every step below has a measured basis on chr21, and none has been run at
chr2 scale or validated against panCollapse's projection fixtures.

**The artifacts.** Keep the exact graph as the source of truth and build two
companion structures from the guide GBWT that `vg rna` already produces:

    vg gbwt -x <chr>.genic.pg -g <chr>.gbz <chr>.guide.gbwt     # 3:30.59, 46.35 GiB on chr21
    vg gbwt -r <chr>.ri       <chr>.guide.gbwt                   # 0:40.62,  3.37 GiB on chr21

On chr21 that is 622,882,936 + 565,177,017 bytes against a 37,068,417,249-byte
PackedGraph -- 31.2x smaller carrying the same 5,607,688 transcript walks. Path
names round-trip exactly, so panCollapse's opacity requirement ("`_H<n>`/`_R<n>`
are never stripped") is met without an adapter.

**The query.** Replace the per-node step enumeration with a bulk r-index locate:

    gbwt::SearchState st = index.find(GBWTGraph::handle_to_node(h));
    std::vector<gbwt::size_type> threads = r_index.locate(st);

Measured 1,297.5 us per node against 192,086 us for plain GBWT locate and
325,873 us for `GBWTGraph::for_each_step_on_handle` -- 148x better than the
former. Per thread the cost is 0.598 us, close to optimal, which leaves answer
size -- not query speed -- as the remaining lever.

**The part that actually has to change.** 1,297.5 us is still 26x over
panCollapse's own 50 us median-lookup threshold, and the entire excess is the
size of the answer: 1,297.5 / 0.598 = 2,169.48 threads per node on the r-index
query benchmark's own sample -- close to, not identical to, the 2,166.62 mean
from the larger 50,000-node sample above, because the two are different
samples of the same graph. Either way it is roughly 792 haplotype-specific
copies of ~2.7 overlapping transcript models. Since
0.598 us x (threads that genuinely visit the node) is a floor, the only remaining
lever is to return fewer identities.

For gene-level counting the haplotype factor is not signal. Carry a
`gbwt::size_type -> gene_id` array (5,607,688 entries, a few tens of MB), map the
located thread ids through it, and return the distinct gene set. Effective answer
size falls from ~2,167 to ~2.7. **This changes what the query returns, not what
the graph contains** -- the exact rule still preserves every byte-distinct
transcript, including the 398,365 bp on chr21 that the OR rule deletes.

**What this path does not solve, and must be measured before adopting it.**

- Positions. `GBWTGraph` is not a `PathPositionHandleGraph`, and panCollapse's
  projection uses `for_each_step_position_on_handle` and `get_position_of_step`.
  Walking the thread is not a substitute: mean 1,645.12 hops at 266.85 us,
  because the graph averages 16.8 bp per node. Either a position structure is
  needed for the paths panCollapse projects against, or the projection has to be
  reformulated. This is the open blocker on the route.
- The 2.7-models-per-node figure is arithmetic (2,166.62 / 792), not a direct
  count of distinct models at a node; the gene mapping needed to measure it
  directly lives in panCollapse's identity ledger, not in the GBWT metadata.
- Nothing here has been run against panCollapse's projection fixtures, which its
  decision record names as the acceptance condition for any GBZ/GBWT route.

## What this assessment does not establish

- chr1 is unmeasured and is larger than chr2. Every whole-genome figure is a
  projection from at most two chromosomes.
- The 155 h serial prune assumes prune wall scales linearly in graph bytes from
  two points.
- The global-GBZ figure is a single-input projection. GBZ size is dominated by
  the GBWT (611 of 623 MB on chr21), which scales with total thread steps and
  haplotype compressibility rather than PackedGraph bytes; the two track on
  chr21, and the joint 23-chromosome GBWT's compressibility is unmeasured.
  "Order of tens of GB" is the claim, not 35 GB.
- **mpmap throughput on a GBZ versus an XG is not measured.** The 5/5-read smoke
  test establishes loadability and correctness on five reads, not speed, and vg
  itself warns "Graph is not in XG format. XG format is recommended for most
  mapping tasks." The GBZ is the artifact that *fits*; that it maps at
  acceptable speed is a separate, unanswered question.
- **A distance index cost is measured from both a GBZ and an XG (2:31.32 at
  8.06 GiB versus 3:45.47 at 60.59 GiB on chr21), and the GBZ-built one has now
  been opened by a mapper.** A rebuild on 2026-09-20 gave 2:07.95 at 7.83 GiB,
  reproducing peak RSS to 2.9% and wall to 15%; supplying it to mpmap as `-d`
  cut the five-read run to 19:17.85 from 27:29.23, at unchanged peak, and
  enriched the multipath output without moving any read (section 16 of
  `RECEIPTS.md`). The **XG-built** index has still not been opened, so no `-d`
  XG-versus-GBZ *mapping* comparison exists. Whether `-d` removes reliance on
  `ref_path_handles`' longest-path heuristic is still unmeasured.
- **No route to a correctly-sensed production GBZ exists in shipped `vg`.**
  `--set-reference` only reaches REFERENCE-sense paths; GENERIC is assigned by
  PanSN naming and no flag overrides it. The naming change that would fix this
  is unbuilt.
- **`ref_path_handles`' longest-path-per-component choice on a transcript-only
  graph has not been checked against spliced-alignment accuracy at scale.** It
  ran without error or a visible MAPQ collapse in a five-read smoke test; that
  is not evidence it is correct on a production graph.
- **If `rpvg` stays in the joint pipeline, its XG requirement (`src/main.cpp:437`,
  "Graph (xg format) input required") defers the XG-to-GBZ saving rather than
  eliminating it.** This assessment does not establish whether rpvg can be
  dropped from the pipeline or given a different input.
- The GBZ drops 25,251 edges (0.926%) relative to the source graph -- every edge
  no annotated transcript or retention pad crosses. Neither the mapping cost of
  that loss nor whether GCSA2 indexes k-mers crossing those edges is measured.
- panCollapse's position query is unsolved. `GBWTGraph` is not a
  `PathPositionHandleGraph`; `PackedPositionOverlay` over all paths is
  O(total steps); and walking the thread is refuted at 1,645.12 mean hops.
- The gene-collapse proposal is arithmetic (2,166.62 / 792 = 2.736), not a
  direct count of distinct models per node, and has not been implemented. The
  792 haplotype count is carried from `RECEIPTS.md` and has not been
  reconciled against the 461 haplotypes `vg gbwt -M` reports for this same
  guide GBWT (above, "Names round-trip exactly"); dividing by 461 instead
  gives 4.70 transcript models per node, which still leaves the gene-collapse
  conclusion (~2,167 candidates collapsing to single digits, far under
  panCollapse's threshold of 500) unchanged, but the discrepancy itself is
  unresolved.
- Nothing here has been run against panCollapse's projection fixtures, which its
  decision record names as the acceptance condition for any GBZ/GBWT route.
- Two of the four GBZ-reachable stages named under "Two graphs, and what GBZ
  changes" are weaker than "measured saving" implies: dropping `vg rna -r`
  is verified only on a 37-node fixture, and `vg rna -b -g`'s net wall-clock
  change versus the separate guide-GBWT stage is unmeasured and could be
  negative (`GBZ_INDEXING_SURVEY.md`).
- Nothing here authorizes a whole-genome run, a GCSA2 run, or any annotation or
  paralog policy change.

## The recipe, as measured, and its correction

The recipe as originally written here was:

    vg gbwt -x <chr>.genic.pg --set-reference CHM13 -g <chr>.gbz <chr>.guide.gbwt
    vg gbwt -r <chr>.ri <chr>.guide.gbwt

`--set-reference CHM13` is still **worth applying**, though nothing measured
shows it is strictly necessary: the untagged run was manually stopped at
147.09 GiB and still climbing, so its true peak is unknown, and the tagged run
completed at 154.55 GiB, not lower. A GBZ built without the tag inherits
`reference_samples` listing all 230 HPRC samples, every HST path becomes
REFERENCE- or GENERIC-sense (1,398,636 and 4,209,052 respectively), and
mpmap's `ReferencePathOverlay` indexes all 5.6M of them regardless of which;
the tag's measured effect is a 25% cut to the overlay's input (below), not a
demonstrated reduction in peak RSS. **But it is not
sufficient, and calling it "part of the recipe, not optional cleanup" without
that qualifier overstated it.** Measured directly on the tagged GBZ
(`chr21.ref.gbz`): 0 REFERENCE paths and 4,209,052 GENERIC paths, all still
indexed by the overlay, because `get_sample_sense`
(`deps/gbwtgraph/src/utils.cpp:174-188`) assigns GENERIC by PanSN naming, which
`reference_samples` cannot touch. The mapped run against this tagged GBZ still
peaked at 154.55 GiB ("The GBZ as mpmap's graph index," above). A joint GBZ
assembled from 23 per-chromosome guide GBWTs would carry the same defect at
whole-genome scale.

**What "mpmap reads the reference-sense CHM13 paths through the overlay"
claimed, and what is actually true.** No path in `chr21.ref.gbz` is CHM13-sense
or even REFERENCE-sense -- CHM13 is not a sample in this guide GBWT at all, so
`--set-reference CHM13` removes the REFERENCE bucket without adding a
reference. What the overlay indexes instead is the GENERIC bucket: retention
pads and non-PanSN-named transcripts. That bucket is smaller (4,209,052 against
5,607,688) but still O(total steps) over transcript-scale path counts. The
actual fix is the naming change identified in item 1 above: name the guide GBWT
so only the chromosome reference is PanSN-qualified as reference or generic,
and every transcript walk carries a sample field that resolves to HAPLOTYPE. No
shipped `vg` flag builds that GBWT today.

**panCollapse is unaffected by any of this.** It reads the haplotype-sense HST
threads through `FastLocate`, and `find`, `extend` and `locate` are indifferent
to path sense -- the naming fix above, once built, would serve both consumers
from one artifact set.

This recipe is deliberately manual for one more reason: `vg autoindex -w mpmap`
given a GBZ input silently plans prune *without* unfolding -- the recipe at
`src/index_registry.cpp:3642` ("Pruned Spliced VG") routes to the same
`prune_graph` function as the unfolding recipe below it, at `:3656`
("Haplotype-Pruned Spliced VG"), but its own progress
message omits "with GBWT unfolding," and the survey confirmed the omission is
real by tracing an autoindex `-d` dry run, not just the wording. Taking that
route would delete every thread-unsupported edge -- the splice-junction
evidence this fork's investigation exists to find -- with no warning.
`GBZ_INDEXING_SURVEY.md` has the detail. Do not substitute autoindex for the
explicit `vg prune -u` step.

## Open questions, in the order they should be answered

1. **What naming change at the guide-GBWT stage yields a correctly-sensed
   GBZ?** The `reference_samples` tag is not sufficient ("The GBZ as mpmap's
   graph index," item 1): GENERIC is assigned by PanSN naming, which no shipped
   flag overrides. This gates production use of GBZ as mpmap's `-x` and is a
   naming-and-tagging change, not a `vg` code change -- but no shipped route
   reaches it yet.
2. **Does a correctly-sensed GBZ map reads at production scale, with correct
   splice accuracy, and does `ref_path_handles`' longest-path-per-component
   choice hold up once the graph carries a real reference?** The five-read
   smoke test is positive (5/5 mapped, MAPQ 60, genuine multipath structure)
   but is not this answer.
3. **mpmap throughput, GBZ versus XG, on a realistic read set with `-d`
   supplied.** Both a GBZ and an XG now exist for chr21, and both distance
   indexes are built; the comparison that would use them has not been run.
4. **Does the GBWT's compressibility hold across a joint 23-chromosome build?**
   The whole-genome GBZ projection rests on this one assumption.
5. **Does `rpvg` stay in the joint pipeline, and if so, is its XG requirement
   removable?** `rpvg/src/main.cpp:437` requires an XG input today. If it
   stays, the XG-to-GBZ saving on `-x` is deferred, not eliminated, regardless
   of what mpmap itself accepts.
6. **What are the 25,251 edges the GBZ drops, and does GCSA2 index k-mers that
   cross them?** Neither is measured.
7. **panCollapse's position query**, which is the blocker on the counting-graph
   route and is independent of everything above.


## The overlay has no cheap lever (2026-09-20, supersedes the naming-change proposal)

An earlier revision of this document, and the pipeline review, both proposed that renaming
transcript paths so they are not PanSN-parseable would take them out of
`PackedReferencePathOverlay`'s index. **Both were wrong.** The overlay indexes REFERENCE
*and* GENERIC (`deps/libbdsg/bdsg/src/reference_path_overlay.cpp:29-31`), and
`get_sample_sense` (`deps/gbwtgraph/src/utils.cpp:174-188`) assigns GENERIC to exactly the
paths whose names carry no sample field. Removing PanSN structure moves a path from
REFERENCE to GENERIC, which changes nothing.

Three bounds on what any sense-based approach can achieve:

- The `reference_samples` tag reaches only the **1,398,636** REFERENCE paths of chr21's
  5,607,688. The remaining **4,209,052**, or **75.1%**, are GENERIC by naming and no tag
  can move them.
- `vg rna` creates output paths by name (`src/transcriptome.cpp:3804`) and the `_R1` copy
  suffix breaks the PanSN regex, so transcripts are GENERIC in the `.pg` regardless. The
  distinction exists only in the GBZ.
- Any genuine reference sample (GRCh38, CHM13) cannot be removed from `reference_samples`
  without demoting the chromosome paths mpmap needs. How many of the 1,398,636 that covers
  was not determined.

**Node survival does not depend on path sense**, so the change is safe -- just ineffective.
See `exact_dedup_indexing_feasibility/RECEIPTS.md` section 15.
