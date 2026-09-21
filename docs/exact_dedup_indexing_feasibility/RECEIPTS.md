# chr21 exact GBZ fixture: measured receipts

Date 2026-09-20. Binary `./bin/vg` SHA256
`4f495d705c5547a39d1334a9c6cd7d4e02ece9e50fe65831ea79ba2679b4273c`
(the pinned production binary). Inputs are the retained chr21 exact arm at
`/mnt/ssd/lalli/hprc_v2_vg_rna/notes/evidence/chr21_exact_arm_20260914/exact/`.
Retained artifacts: `/mnt/ssd/lalli/hprc_v2_vg_rna/notes/evidence/chr21_gbz_indexing_fixture_20260920/`
(moved there from the ephemeral job directory on 2026-09-20; see its README.md).

## 1. Name preservation through GBWT metadata -- PASS

`vg gbwt -M guide.gbwt`:
    5,607,688 paths with names, 231 samples with names, 461 haplotypes,
    5,599,920 contigs with names
Load 4.23 s at 1,089,536 KiB.

`vg gbwt -T guide.gbwt` produced 5,607,688 names. Compared as sorted sets
against the arm's own `guide.names` (the XG-side names): **identical**.
Every name carries the `_R<n>` suffix (5,607,688 of 5,607,688); none carries
`_H<n>`. Forms seen: `__panSC_retention_pad1000__<sha256>_{L,R}_R1` for
retention pads and `<sample>#<hap>#<sample>_{ha,pa}_T<id>_R1` for biological
HST paths.

This satisfies panCollapse's contract requirement that names are opaque and
`_H<n>`/`_R<n>` are never stripped.

## 2. GBZ construction and size

    vg gbwt -x genic.pg -g chr21.gbz guide.gbwt
    wall 3:30.59, peak RSS 48,602,136 KiB (46.35 GiB), exit 0

| artifact | bytes | |
|---|---:|---|
| `genic.pg` (PackedGraph, paths embedded) | 37,068,417,249 | 37.07 GB |
| `guide.gbwt` | 611,729,792 | 0.61 GB |
| `chr21.gbz` | 622,882,936 | 0.62 GB |

The GBZ carries the same graph and the same 5,607,688 transcript walks in
**59.5x less space** than the PackedGraph, and is only 11,153,144 bytes larger
than the GBWT alone -- that delta is the entire node-sequence payload
(34,459,835 bp).

## 3. Per-node thread multiplicity (50,000 nodes sampled, seed 20260920)

GBZ load 22.20 s; probe peak RSS 2,259,948 KiB.

| statistic | threads visiting a node |
|---|---:|
| mean | 2,166.62 |
| median | 344 |
| p95 | **7,453** |
| max | 136,523 |

All 50,000 sampled nodes carried at least one thread.

**panCollapse's own bottleneck threshold is "p95 projected path occurrences per
aligned node exceeds 500"** (`docs/research/annotation-lookup-performance.md`).
The measured p95 is 7,453 -- about **15x over that threshold** -- on chr21,
the smallest chromosome, under the exact rule.

## 4. Single-branch discrimination at a uniformly sampled node is nil

| statistic | value |
|---|---:|
| edge multiplicity mean | 1,646.17 |
| edge multiplicity median | 119 |
| edge multiplicity p95 | 6,093 |
| thread-carrying branches per node, mean | 1.314 |
| thread-carrying branches per node, p95 | 2 |
| best-branch threads / node threads, median | 1.000 |
| best-branch threads / node threads, p05 | 0.757 |

**Read this narrowly.** Nodes were sampled uniformly, and a uniformly sampled
node in this graph is overwhelmingly exon-interior: the mean number of
thread-carrying branches is 1.314, so most sampled nodes have exactly one
successor and no branch to discriminate on. That is why the best branch carries
all threads at the median node. The measurement therefore establishes one thing:
**at the median node no discrimination is available from its out-edges by any
method**, because there is only one out-edge.

It does **not** test whether splice junctions discriminate. A splice junction is
an edge that skips an intron, and the relevant population is edges whose source
has two or more thread-carrying branches -- roughly 31% of nodes by the mean
above -- and reads that actually cross one. A probe that samples only branching
nodes and reports the minority-branch fraction would answer that; it has not
been run. Treat junction-first indexing as untested, not as refuted.

## 5. GBWT search-state extension is fast

`find(node)` plus `extend()` over every thread-carrying branch: **0.873 us per
node**. A full 24-hop walk with branch evaluation at every hop costs
**14.54 us**, over a mean walk of 517.9 bp -- read-length scale.

## 6. Extending along a read does NOT collapse the candidate set

20,000 random walks, extending the SearchState one node at a time and following
the most-supported branch at each step:

| hop | median surviving threads | p95 | mean |
|---:|---:|---:|---:|
| 0 | 345 | 7,327 | 2,155.2 |
| 4 | 350 | 6,818 | 2,038.3 |
| 8 | 313 | 6,119 | 1,873.0 |
| 16 | 262 | 5,343 | 1,639.5 |
| 24 | 233 | 4,821 | 1,467.4 |

Over 24 hops and a mean 517.9 bp -- longer than a typical single-cell read --
the median candidate set falls only from 345 to 233 and the mean from 2,155 to
1,467. **68% of candidates survive.** At the 87,586 observed branching events the
minority branches carry a mean of 8.4% of threads (median 1.4%), so following the
modal branch discards almost nothing.

This refutes the hopeful version of the search-state argument: composing
constraints along the read is cheap (14.54 us) but it does not reduce the answer.

## 7. Why: the multiplicity is haplotype redundancy, not transcript ambiguity

    node_mult_mean 2,166.62 / 792 haplotypes = 2.736 transcript models per node
    node_mult_median 344  ->  43.4% of the 792 haplotypes carry the median node

The candidate set is large because it genuinely contains ~792 haplotype-specific
copies of ~2.7 overlapping transcript models. Exact deduplication is precisely
the rule that preserves those copies -- they differ only at the variant sites
that make them non-byte-identical. A read that does not overlap a distinguishing
variant *cannot* separate them, by any index. The redundancy is information, not
an indexing defect.

The consequence for panCollapse is the useful part: for gene-level counting the
haplotype factor is not signal. Collapsing the answer to distinct transcript
models or genes inside the query takes the effective multiplicity from ~2,167 to
~2.7 -- from 15x over panCollapse's p95 threshold of 500 to two orders of
magnitude under it. That is an answer-space change (return model/gene ids, not
path ids), not a graph or index-structure change.

## 8. Query cost, and what the r-index fixes

Enumerating the threads at a node, measured three ways on the same data:

| route | per node | per thread |
|---|---:|---:|
| `GBWTGraph::for_each_step_on_handle` (GBZ) | 325,873 us | 156.62 us |
| plain `gbwt::GBWT::locate()` per position | 192,086 us | 73.23 us |
| **`gbwt::FastLocate::locate(SearchState)` (r-index)** | **1,297.5 us** | **0.598 us** |
| `find` + `extend`, count only, no identities | 0.873 us | -- |

The r-index is **148x faster per node** than plain GBWT locate and 122x faster
per thread. It was built by `vg gbwt -r` in 40.62 s at 3.37 GiB peak, producing
565,177,017 bytes.

    chr21.gbz     622,882,936 B
    chr21.ri      565,177,017 B
    together    1,188,059,953 B  -- still 31.2x under genic.pg's 37,068,417,249 B

**Per-thread cost is now essentially optimal at 0.598 us; the per-node total is
answer-size-bound.** 2,169.48 threads x 0.598 us is the 1,297.5 us. Against
panCollapse's 50 us median threshold, 1.30 ms is still 26x over -- but the excess
is entirely the size of the answer, not the speed of the lookup.

That is what makes the gene-collapse point actionable rather than cosmetic: the
only remaining lever is returning fewer identities. Nothing about the index can
beat 0.598 us x (threads that genuinely visit the node).

Note also `walk_mean_hops` = 1,645.12 at 266.85 us per position query. The
"transcript threads are short, so walking replaces the position index" argument
is **refuted**: the chr21 exact graph holds 34,459,835 bp in 2,056,621 nodes, a
mean node of 16.8 bp, so an ordinary transcript still spans thousands of nodes.

## 9. XG versus GBZ on the same input -- measured

`vg index -x -t 24` on the same `genic.pg`, pinned binary, 400 GiB cap:
**54:28.44 wall, 71,850,656 KiB (68.52 GiB) peak RSS, exit 0.**

| | `vg index -x` | `vg gbwt -g` (GBZ) | ratio |
|---|---:|---:|---:|
| wall | 54:28.44 | 3:30.59 | **15.5x faster** |
| peak RSS | 68.52 GiB | 46.35 GiB | 1.48x less |
| artifact | 55,013,545,597 B | 622,882,936 B | **88.3x smaller** |

With the r-index included (GBZ + `.ri` = 1,188,059,953 B) the size ratio is
still **46.3x**.

The measured `xg/pg` ratio is **1.484**, against the 1.427 the whole-genome
driver assumed from an OR-rule graph -- 4% apart, so the driver's figure was
sound and the projection built on it stands with a small correction.

## 10. What that does to the whole-genome projection

Using the measured 1.484 and the projected 2.10 TB exact corpus:

| | projected |
|---|---:|
| global XG | **3.12 TB** = 2.88x the 1,007 GiB host |
| global XG construction | ~51 h |
| global GBZ | order of tens of GB (~35 GB point estimate) |
| global GBZ construction | ~3.3 h |

**The global XG is the stage that makes whole-genome exact infeasible, and it is
now infeasible on a measured ratio rather than a borrowed one.** The GBZ is the
only candidate measured to fit.

Caveat on the GBZ projection: 88.3x and the corpus scaling are single-input
figures. GBZ size is dominated by the GBWT (611 of 623 MB on chr21), which scales
with total thread steps and haplotype compressibility, not with PackedGraph
bytes. Those track on chr21; the joint 23-chromosome GBWT's compressibility is
unmeasured. Treat "tens of GB" as the order, not 35 GB as the number.

## 11. The reference_samples tag decides whether a GBZ is usable by mpmap

`vg gbwt -Z --tags` on the GBZ built plainly from `guide.gbwt`:

    reference_samples   HG00097 HG00099 ... NA21309      (230 samples)
    source              jltsiren/gbwt

**All 230 HPRC samples are marked reference**, so essentially all 5,607,688 HST
paths carry REFERENCE sense, not HAPLOTYPE sense. CHM13 is the only sample *not*
listed.

That inverts the expected failure mode. `mpmap` applies a
`ReferencePathOverlayHelper`, and `PackedReferencePathOverlay` indexes
GENERIC and REFERENCE sense paths. With 5.6M reference-sense paths it builds a
`PackedPositionOverlay` over all of them -- the per-path `steps_0`/`steps_1`/
`positions`/boomphf/`step_positions` structure that is O(total steps).

Observed: `vg mpmap -x chr21.gbz -g chr21.gcsa` on **5 reads** grew
2.07 -> 36 -> 95.71 -> 147.09 GiB over 11 minutes and was still climbing when
stopped. `vg sim -x chr21.gbz` reached 221 GiB on the same file. Neither is a
mapping cost; both are the overlay.

Rebuilding with `vg gbwt -x genic.pg --set-reference CHM13 -g chr21.ref.gbz`
took 2:07.91 at 46.24 GiB and produced 622,881,552 bytes, whose tags read:

    reference_samples   CHM13
    source              jltsiren/gbwt

That should bound the overlay to CHM13's 2,737 chr21 paths rather than
5,607,688. **The GBZ's on-disk size is not the operative quantity for mpmap; the
reference_samples tag is.** A GBZ 88.3x smaller than the XG can still cost more
resident memory than the XG if every path in it is reference-sense.

## 12. GBZ topology fidelity: all nodes kept, 0.93% of edges dropped

    vg stats -N -E chr21.gbz   ->  2,056,621 nodes   2,701,234 edges
    source exact chr21 graph   ->  2,056,621 nodes   2,726,485 edges

Node count is **identical**. Edge count is **25,251 lower, 0.93% of the graph's
edges**. A GBWTGraph's edge set is what the GBWT's node records support, so edges
that no thread traverses do not survive into the GBZ.

**This is a semantic difference from an XG, not just a size difference.** The
guide GBWT carries transcript and retention-pad walks, so the dropped edges are
those crossed by no annotated transcript and no retention pad. Two consequences
that pull in opposite directions:

- For mapping *to the annotated transcriptome*, edges no transcript uses are
  arguably not needed, and losing them is harmless.
- For **novel splice-junction discovery** -- which is the stated purpose of this
  fork's `vg mpmap --trace-splice-search` instrumentation -- an edge crossed by
  no *annotated* transcript is precisely the object of interest. Building the
  mapping index from an annotation-derived GBWT would remove the evidence the
  investigation is looking for.

It also sits awkwardly against the project's reason for choosing exact
deduplication, which is that a single base can be the only thing separating two
segmental-duplication copies. A route that silently drops 25,251 edges deserves
an explicit decision rather than an inherited default.

Not yet established: what those 25,251 edges are (variant edges in uncovered
regions? flank boundaries?), and whether including a haplotype GBWT alongside the
transcript guide in the GBZ would restore them.

## 13. mpmap's startup cost: the overlay dominates, and reference_samples cannot fix it

Full startup timeline of `vg mpmap -x chr21.ref.gbz -g chr21.gcsa` (no `-d`):

    0.0 m  Loading graph
    3.3 m  Completed loading graph
   17.6 m  Identifying reference paths          <- 14.3 min gap = overlay_helper.apply()
   18.9 m  Completed loading GCSA2 / LCP
   19.0 m  Labeling embedded paths by their connected component
   22.5 m  Building null model
   27.2 m  Mapping reads
   27:29.23 total, 162,068,556 KiB (154.55 GiB) peak, exit 0

**Two distinct costs, and the larger one is the overlay.** The 3.3 -> 17.6 m gap is
`overlay_helper.apply()` at `src/subcommand/mpmap_main.cpp:1830-1831` -- **14.3 minutes**,
four times the 3.5-minute component-labeling pass. An earlier revision of this section
claimed the overlay was *not* the mechanism; that was wrong and is retracted.

**Why `--set-reference CHM13` barely helped.** `PackedReferencePathOverlay` indexes
REFERENCE *and* GENERIC paths, and `get_sample_sense`
(`deps/gbwtgraph/src/utils.cpp:174-188`) assigns GENERIC to any path whose name carries no
PanSN sample field -- which `reference_samples` cannot override. Counted directly on the
GBZ that was mapped against:

| | REFERENCE | GENERIC | indexed by the overlay |
|---|---:|---:|---:|
| `chr21.gbz` | 1,398,636 | 4,209,052 | 5,607,688 |
| `chr21.ref.gbz` | 0 | 4,209,052 | 4,209,052 |

(2,803,513 `__panSC_retention_pad1000__...` walks plus 1,405,539 non-PanSN names make up the
GENERIC 4,209,052; 1,398,636 PanSN transcript names make up the REFERENCE set, and they fall
to HAPLOTYPE once `reference_samples` is CHM13, a sample this GBWT does not contain.)

So the tag cut the overlay's input by 25% and the two runs tracked accordingly -- 38 vs
36 GiB at 8 min, 102 vs 95.7 GiB at 10 min. **No cheap fix exists** -- see section 15; the
obvious naming change moves paths from REFERENCE to GENERIC, both of which the overlay
indexes.

**The second cost, the component-labeling pass, is separately avoidable.**
`mpmap_main.cpp:2013-2016` gates it on `distance_index_name.empty() &&
get_path_count() > 0`, so supplying `-d` removes it.

**A semantic note that matters more than either cost.** `ref_path_handles`
(`mpmap_main.cpp:1845-1855`) does **not** consult path sense. For each connected component
it inserts the single *longest* path. On a graph whose only paths are transcripts and
retention pads, mpmap therefore adopts the longest transcript per component as that
component's "reference" for spliced alignment, silently and without warning. That is not a
crash and not a MAPQ collapse, but it means splice-strand context is being taken from an
arbitrary transcript rather than from a reference sequence. Any production use of a
transcript-only GBZ as mpmap `-x` should establish what this does to spliced accuracy.

## 14. End-to-end: a GBZ works as mpmap's graph index

    vg mpmap -x chr21.ref.gbz -g chr21.gcsa -f reads.fq -t 4   (no -d)
    27:29.23 wall, 162,068,556 KiB (154.55 GiB) peak RSS, exit 0, 986-byte GAMP

Reads were five 150 bp windows cut at 100 bp stride from a real 585 bp transcript
(`NA21110#1#NA21110_ha_T0366374_R1`) extracted from the GBZ itself.

| read | MAPQ | subpaths | nodes visited |
|---|---:|---:|---:|
| read0 | 60 | 1 | 8 |
| read1 | 60 | 3 | 9 |
| read2 | 60 | 1 | 15 |
| read3 | 60 | 1 | 6 |
| read4 | 60 | 3 | 6 |

**5/5 mapped with non-empty subpaths at MAPQ 60**, and reads 1 and 4 carry genuine
multipath structure (read1 has two start subpaths), so mpmap is producing multipath
alignments rather than degenerate single-path ones.

The GCSA2 used was the chr21 exact arm's own, built as
`vg index -g ... -f mapping.throwaway ... pruned.vg`; the `-f` mapping puts its node IDs
back in the original space, so it is compatible with a GBZ built from `genic.pg`.

**What this establishes:** a GBZ is loadable and usable as `vg mpmap -x`, and produces
valid multipath alignments. **What it does not establish:** accuracy, throughput, or
behaviour at scale. Five reads from one transcript is a smoke test. The 27:29 wall and
154.55 GiB peak are dominated by the missing-distance-index fallback of section 13, not by
mapping, so neither figure characterises mpmap-on-GBZ performance.


## 15. Path sense does not govern node survival, and the overlay has no cheap lever

Two questions, settled together on 2026-09-20 by source reading plus a controlled fixture.

### Does making transcript and pad paths HAPLOTYPE-sense cause node deletion?

**No, on the route this project runs.**

`vg rna -d/--remove-non-gene` destroys every embedded path before deciding anything
(`src/transcriptome.cpp:3550-3559`, with `assert(_graph->get_path_count() == 0)`), then
deletes on node-ID membership alone: `if (transcribed_nodes.count(_graph->get_id(handle))
== 0)` (`:3572`), where `transcribed_nodes` is filled from `_transcript_paths`, the
in-memory GFF3-derived set. Dependent on neither sense nor name.

`vg prune -u` deletes when the graph holds zero paths: `_alt_` paths are dropped by name
(`src/subcommand/prune_main.cpp:483`, a literal prefix test via `src/path.cpp:14-19`), an
XG is built with `destroy_all_paths` as its `on_input_consumed` callback, and only then do
`prune_complex_with_head_tail` and `prune_short_subgraphs` run. Restoration is
`PhaseUnfolder::unfold`, which touches the GBWT only structurally --
`grep -cE 'metadata|tags|reference_samples' src/phase_unfolder.{cpp,hpp}` returns **0 and
0**, verified directly. The `reference_samples` tag is precisely what a sense change flips,
and the unfolder cannot reach it.

Controlled fixture: one guide GBWT carrying a biological walk and a pad walk, run under
three `reference_samples` settings (none, both samples, biological-only), produced `.pg`
and `.mapping` files with **identical SHA256** in all three arms, against a no-guide control
that loses the pad.

**The premise the question rested on is also inverted.** All 2,803,513 chr21 retention pads
are already GENERIC, never REFERENCE, and have been protected in that state throughout this
generation. Protection comes from being fed into `vg rna` as GFF3 features, and from being
carried as threads in the guide GBWT -- not from reference status.

### The one real sense-dependent deletion path, currently unreached

`PhaseUnfolder::restore_paths` (`src/phase_unfolder.cpp:73-75`) filters to
`{PathSense::GENERIC, PathSense::REFERENCE}`, with the comment "we include generic to also
pick up transcript paths". HAPLOTYPE-sense paths are dropped silently at exit 0. Its two
call sites are `src/subcommand/prune_main.cpp:543`, guarded to `vg prune -r`, and
`src/index_registry.cpp:3576`, inside `vg autoindex`. Every script in this project uses
`prune -p -u`, so this is **unreached today** -- and becomes live the moment a pad-carrying
graph is routed through `vg autoindex` or `prune -r`.

### Why the overlay cost cannot be fixed cheaply

| lever | reaches | leaves indexed |
|---|---:|---:|
| `reference_samples` tag | 1,398,636 REFERENCE | 4,209,052 GENERIC (**75.1%**) |
| removing PanSN `#` from names | nothing -- REFERENCE becomes GENERIC | all 5,607,688 |

`get_sample_sense` (`deps/gbwtgraph/src/utils.cpp:174-188`) assigns GENERIC to a path whose
sample field holds the magic generic sample, and a tag cannot move a path that has no
sample. `PackedReferencePathOverlay` indexes REFERENCE and GENERIC alike
(`deps/libbdsg/bdsg/src/reference_path_overlay.cpp:29-31`).

`vg rna` also creates output paths by name (`src/transcriptome.cpp:3804`), and the `_R1`
copy suffix lands after the PanSN phase-block field and breaks the regex, so transcripts
emerge GENERIC in the `.pg` whatever the tag says. The sense distinction exists only in the
GBZ.

Not established: how many of the 1,398,636 REFERENCE paths belong to genuine reference
samples that cannot be demoted without losing the chromosome paths mpmap needs; and whether
the 14.3-minute overlay cost scales linearly in path count, which was never measured and
should not be assumed, since the overlay indexes steps rather than paths.

## 16. A mapper opens a GBZ-built distance index: startup 27:29 -> 19:18, output enriched

Added 2026-09-20 evening, after the sections above. Pinned binary
`4f495d705c5547a39d1334a9c6cd7d4e02ece9e50fe65831ea79ba2679b4273c` (hash re-verified against
`bin/vg` before the runs), `TMPDIR` on SSD. Artifacts and GNU-time receipts in
`/mnt/ssd/lalli/hprc_v2_vg_rna/notes/evidence/chr21_gbz_indexing_fixture_20260920/`.

**Build.** Positional form, since `-x` is hard-typed to `xg::XG`
(`src/subcommand/index_main.cpp:778`):

    vg index -t 24 -j chr21.dist chr21.ref.gbz

Exit 0, **2:07.95 wall, 8,209,496 KB = 7.83 GiB peak**, 697% CPU, 1,864,658,896-byte index.
Receipt `dist_gbz.time.txt`. Built from `chr21.ref.gbz` rather than `chr21.gbz` because that
is the graph mpmap opens.

This is the **second** measurement of the GBZ arm and the first with a retained artifact --
the earlier arm-to-arm pair (2:31.32 at 8.06 GiB from the GBZ, 3:45.47 at 60.59 GiB from the
XG) kept neither a `.dist` nor a receipt. The `gbz_fixture/` working directory it ran in was
moved into the evidence store on 2026-09-20 and no distance-index artifact or receipt came
with it; the surviving job directory `/mnt/ssd/lalli/.claude/jobs/6a731a8b/tmp/` holds no
distance-index receipt either. Where that pair was run is therefore not recorded, its thread
count cannot be recovered, and this is not a strict replicate. Taken as a crude n=2
repeat it is still the only noise information this stage has: **peak RSS reproduces to 2.9%,
wall differs by 15%**. That is consistent with the caveat carried since the arm-to-arm run --
the 7.5x memory advantage over the XG sits far outside that spread; the 1.49x wall advantage
does not.

**A mapper opens it.** Same command as the completed section-14 run, same graph, GCSA2, reads
and `-t 4`, with `-d chr21.dist` the only change:

    vg mpmap -x chr21.ref.gbz -g chr21.gcsa -d chr21.dist -f reads.fq -t 4

Exit 0, **19:17.85 wall**, 162,080,896 KB = **154.56 GiB** peak, 347% CPU, 5 reads mapped.
Receipt `mpmap_ref_dist.time.txt`, output `mpmap_ref_dist.gamp` / `.json`.

| phase | no `-d` (section 14) | with `-d` |
|---|---|---|
| graph load | 0 -> 3.3 m | 0 -> 3.4 m |
| `overlay_helper.apply()` | 3.3 -> 17.6 m (14.3) | 3.4 -> 17.0 m (13.6) |
| reference paths, GCSA2, LCP | 17.6 -> 19.0 m | 17.0 -> 18.5 m |
| component labeling | 19.0 -> 22.5 m (3.5) | **absent** |
| null-model calibration | 22.5 -> 27.2 m (4.7) | 18.5 -> 18.9 m (0.4) |
| total | 27:29.23 | 19:17.85 |

**The saving is 8:11.38, and more than half of it was not predicted.** The component-labeling
pass gated at `src/subcommand/mpmap_main.cpp:2013-2016` disappears as expected, worth 3.5 m.
The larger unpredicted term is **null-model calibration, 4.7 m -> 0.4 m**, which this project
had not identified as distance-index-sensitive at all. The overlay is unchanged within noise
(14.3 -> 13.6 m) and remains the dominant startup cost; `-d` does not touch it.

**Peak RSS is unchanged: 162,068,556 KB -> 162,080,896 KB, +0.008%.** A 1.86 GB distance index
costs nothing measurable at peak, because peak is set by the overlay, not by the index.

**The output changes, and strictly in one direction.** All 5 reads still map at MAPQ 60. The
multipath structure is markedly richer -- subpaths per read 1, 3, 1, 3, 1 without `-d` against
10, 12, 16, 7, 8 with it. Per read, the no-`-d` node set is a strict **subset** of the with-`-d`
node set and the node-ID span is identical, so the index adds alignment alternatives rather
than relocating any read. This is the direction vg's own startup warning predicts ("Both
accuracy and speed will suffer without one").

**Limits.** Five reads from one transcript is a smoke test: it establishes that a mapper opens
a GBZ-built distance index, what that does to startup, and that the mapped output is enriched
rather than moved. It establishes nothing about accuracy or throughput at scale, and one run
per arm carries no noise floor for the mpmap wall figures. The **XG-built** distance index has
still never been opened by a mapper, so no `-d` XG-versus-GBZ mapping comparison exists -- only
the construction-cost comparison above.
