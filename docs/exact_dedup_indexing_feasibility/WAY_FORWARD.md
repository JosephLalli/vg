# Way forward for exact-dedup mpmap indexing

Written 2026-09-20 against the measured record in `RECEIPTS.md` (15 sections), the verified
survey in `GBZ_INDEXING_SURVEY.md`, and the failed pipeline in
`MPMAP_INDEX_PIPELINE_PROPOSAL.md`. This is a proposal. One item has since been
authorized and run -- the GBZ-built distance index under "Do now" below, on 2026-09-20.
Everything else here remains unauthorized, including the two checks below that must pass
before any chr2 or GCSA2 work.

## What is settled, so nobody re-derives it

- **`vg mpmap` is GCSA2-only.** All three MEM seeders and the splice scorer are GCSA2
  queries; it hard-exits without `-g`. A GBZ replaces its `-x`, and neither prune nor GCSA2.
- **Prune cannot be removed by any GBZ route.** A GBZ drops only the edges no thread
  traverses -- 0.926% on chr21 -- which is nowhere near GCSA2's complexity bound.
- **A junctions-only alignment graph does not survive prune.** Without embedded transcript
  paths, nothing protects the splice junctions from `prune_complex_with_head_tail`, because
  haplotype threads do not skip introns.
- **Node IDs force one `vg rna` run.** `vg rna` output node IDs are not reproducible above
  `-t 1`, and panCollapse requires the graph to be in the same node-ID space as the GAMP.
  An alignment build and a counting build cannot be separate invocations.
- **Path sense does not govern node survival**, and the overlay cost has no cheap lever.
  See `RECEIPTS.md` section 15.

## Do now, independent of every open question

**Adopt the distance index built from a GBZ.** Measured side by side on chr21, one build from each source: **2:31.32 at
8.06 GiB** from the GBZ against **3:45.47 at 60.59 GiB** from the XG -- 7.5x less peak
memory. Use the positional form, since `-x` is hard-typed to `xg::XG`
(`src/subcommand/index_main.cpp:778`):

    vg index -j out.dist graph.gbz

Nothing else depends on this, it works with stock vg, and supplying `-d` to mpmap also
skips the component-labeling startup pass gated at `src/subcommand/mpmap_main.cpp:2013-2016`.
Caveat: one run of each build on a loaded host with no noise floor. The 7.5x memory ratio is far
outside plausible contention; the 1.49x wall ratio is not.

**Done 2026-09-20, and it paid more than predicted.** `vg index -t 24 -j chr21.dist
chr21.ref.gbz`: exit 0, 2:07.95 at 7.83 GiB, 1.86 GB index. Feeding it to the section-14
mpmap smoke test as `-d`, with nothing else changed, took the run from 27:29.23 to
**19:17.85** at unchanged peak RSS (154.56 GiB, +0.008%), with all five reads still at
MAPQ 60 on a strictly enriched multipath DAG. Of the 8:11 saved, the predicted
component-labeling deletion is 3.5 m; the larger term is **null-model calibration,
4.7 m -> 0.4 m**, which was not predicted and had not been identified as
distance-index-sensitive. The overlay is untouched at 13.6 m and still dominates startup.
Receipts and limits: `RECEIPTS.md` section 16.

## What has already been run end to end, and what that does to the transcript-sequence consistency check

Found 2026-09-23 in the stage receipts of the retained September 14 chr21 exact-deduplication
run, and it narrows both prerequisite checks.

That run is a complete chain from a chromosome isolated out of the
whole-genome pangenome to a finished GCSA2, all exit 0, all receipts retained in
`notes/evidence/chr21_exact_arm_20260914/exact/*.time.txt`:

| stage | command shape | wall | peak RSS |
|---|---|---|---|
| project | `project_cat_transcript_annotation.py` | -- | -- |
| rna | `vg rna -t 24 -p -z -j -c no -s Parent -y exon -r -d -i raw_info.tsv -n /dev/stdin hprc-v2.1-mc-chm13.full.noHG002_13_chr21.gbz` | 1:01:06 | 99.13 GiB |
| guide | `vg gbwt -p -E -x transcript_full.pg -o guide.gbwt` | 1:03:26 | 49.06 GiB |
| strip | `vg paths -d -p retention_path_names.txt -x transcript_full.pg` | 18:57.23 | 45.68 GiB |
| prune | `vg prune -p -u -k 32 -M 0 -t 24 -g guide.gbwt -a -m mapping genic.pg` | 4:16:41 | 256.70 GiB |
| gcsa | `vg index -p -g ... -k 16 -X 4 -f mapping --gcsa-* pruned.vg` | 1:46:02 | 25.46 GiB |

The input is a per-chromosome chunk of `hprc-v2.1-mc-chm13.full.noHG002`, so the isolation
step is real and upstream of this table. The 2026-09-20 indexing and mapping work sits on top
of `genic.pg`, `guide.gbwt` and `chr21.gcsa` from this same run.

**This is embed-then-index, but not embed-then-strip.** `vg rna -r` embeds the transcripts and
the 1 kb flank-buffer features alike, and the guide GBWT is built from the *embedded* paths of
the rna output. The strip step then removes **only the flank-buffer paths** --
`retention_path_names.txt` is written by `partition_vg_info.py` as the buffer partition -- so
the biological transcripts stay embedded in `genic.pg`, and prune ran on a path-embedded
graph. Pruning a graph with its transcript paths removed, the regime the splice-junction
survival check is about, has still never been run.

**It is therefore immune by construction to the stale-numbering defect, which is the only
thing the transcript-sequence consistency check exists to catch.** That defect belongs to
`vg rna -v/--write-hap-gbwt`, which writes a GBWT numbered against node IDs the output graph no
longer uses. `vg gbwt -E` cannot do that: it reads the paths the output graph itself carries,
so it is in the output node space by definition, and `vg paths -d` preserves node IDs. The
consistency check is needed only if the check run takes its guide from `vg rna -b` instead.
If the production route keeps `vg gbwt -E`, the check tests a hazard that route does not have
-- so **decide which guide route the check run is testing before running it**, or it answers a
question nobody is asking.

That argument is from flag semantics -- `--index-paths` reads the graph's own paths,
`--drop-paths` removes labels and not nodes -- and it is corroborated, not proved, by
measurement: `vg stats -N -r chr21.gbz` on the GBZ built from `genic.pg` plus `guide.gbwt`
gives 2,056,621 nodes over a contiguous node-id range 1:2,056,621, matching `genic.pg`'s
recorded 2,056,621 nodes exactly. A guide carrying node IDs from a stale space could not
produce that. What has *not* been done is a direct node-id-range comparison against
`genic.pg` itself, which needs a 35 GB PackedGraph load.

**Two caveats on reading this table as an end-to-end test.** It ran on `vg-pinned` from
`whole_genome_runs/wg_genic_rna_chr1_17_20260910T233840/bin/`, not the pinned production binary
`4f495d70...4273c`, so no chain has been run end to end on one binary; and nothing here was
checked for splice-junction survival, which is still unmeasured.

## The two checks that must pass before any chr2 or GCSA2 work

One chr21 run, with the corrected flags, checked on two things nothing in this project has
ever measured.

    vg rna -t 24 -z -j -g -r -c no -s Parent -y exon --progress \
      -b chr21.guide.gbwt -f chr21.tx.fa -i chr21.tx.tsv \
      -n <( stream_rewritten_gffs ) chr21.gbz > chr21.full.pg

`-r` embeds the transcripts, which is what protects the junctions through prune. `-c no` is
required: `-c` defaults to `haplotype`, which the exact-rule runs did not use. Do not use
`-q/--out-exclude-ref` with `-j`: measured, it silently empties the pantranscriptome. Do not
use `-v`: it emits a GBWT in a stale node space under `-j`, and its help text says otherwise.

**Read the command correctly before running it.** Three things about it are easy to get
backwards, and each one changes what the two checks mean.

`-b`, `-f` and `-i` are **outputs**, not inputs -- all three are under "Output options" in
`src/subcommand/rna_main.cpp:94-100` (`--write-gbwt`, `--write-fasta`, `--write-info`). So
`chr21.guide.gbwt`, `chr21.tx.fa` and `chr21.tx.tsv` are produced by this run. The
transcript-sequence consistency check is therefore a comparison between two products of a
single invocation -- the GBWT and the FASTA -- not a comparison against the guide already on
disk from the September 14 run. That is precisely what makes it a test for the stale-numbering
defect: if the emitted GBWT is in a stale node space, its threads spell
different sequences than the FASTA the same run wrote.

The run's only real inputs are the positional chr21 **haplotype** pangenome GBZ and the
rewritten GFF stream. That input is **not** the `chr21.gbz` in
`notes/evidence/chr21_gbz_indexing_fixture_20260920/`: same filename, different object -- the
fixture copy already carries transcripts. Neither the haplotype GBZ nor `stream_rewritten_gffs`
is in a retained fixture; both live in the downstream `hprc_v2_vg_rna` workspace and must be
located and hash-pinned before the run.

**Unresolved: whether `-d/--remove-non-gene` belongs in this command.** As written above it is
absent, which builds the whole chromosome. Owner decision 2 of 2026-09-20 records the opposite
-- that the retention requirement (introns, exons, >=1 kb either side) is met *with* `-d` plus
the `make_retention_features.py` pad features, "not by omitting `-d`", and that the prior
chr2/chr21 **genic+flank** figures therefore transfer. Note `-d` means `--remove-non-gene` in
`vg rna` and `--distance-index` in `vg mpmap`; they are unrelated flags. This must be settled
before the run, because it decides whether the check run measures a whole-chromosome or a
genic+flank graph, and the cost figures the run is supposed to supply are not comparable
across that choice.

**Evidence, found 2026-09-23, that `-d` belongs there.** The retained September 14 chr21
run (`notes/evidence/chr21_exact_arm_20260914/`) already ran this shape, and its `vg_rna.time.txt`
records `vg rna -t 24 -p -z -j -c no -s Parent -y exon -r -d -i raw_info.tsv -n /dev/stdin
<chr21 chunk>.gbz` -- `-d` present, alongside a retained `retention_pad1000.gff3`. That is
owner decision 2's design, already executed, and it is where the genic+flank figures come
from. It does not by itself settle the check-run command, because that run emitted neither `-b`
nor `-f` and so is not the same invocation; but the `-d`-plus-pads combination is not
hypothetical.

**Thread count.** Both checks are relabel-invariant -- name-sorted set equality on sequences,
and an edge count -- so neither needs the fork's `-t 1` byte-identity rule. `-t 24` is
permitted here.

**The transcript-sequence consistency check.** Build a GBZ from `chr21.full.pg` plus `chr21.guide.gbwt`
and require the transcript sequences it yields to match `chr21.tx.fa` as a name-sorted set.
A guide in a stale node space fails this; nothing shipped catches it otherwise --
`vg prune --verify-paths` returns success on a stale index.

**The splice-junction survival check.** Derive the alignment graph by dropping embedded paths
(`vg paths -d`, which removes labels and never nodes, so node IDs are preserved), prune it
with the transcript guide, and count how many of `vg rna`'s junction edges survive into
`pruned.vg`. If survival is not essentially complete, the embed-then-strip design is
mandatory rather than preferred, and a junctions-only alignment graph is dead for good.

The same run also yields, at no extra cost, the prune wall and peak on a path-light graph --
replacing the projection below with a measurement -- the XG cost on a graph that keeps
introns, and an `-x` comparison between the XG and the PackedGraph with `-d` built from the
same file.

## What the win is projected to be, and why the projection is weak

chr2's prune divides into XG construction 6.19 h (48% of 12:47:16, holding a 336.89 GiB path
plateau), `complement_components` 2.70 h (21%, single-threaded), and the unfold 3.63 h (28%).

Dropping embedded paths collapses the XG phase's path payload, which is the 48%. It does
**not** touch `complement_components`, which is topology-driven, and it does **not** reduce
the unfold, whose cost is set by the guide's thread count -- still roughly 15M short threads
under the exact rule. The 341.07 GiB peak falls in the unfold, not in XG construction, so
the expected saving is **wall time, not peak memory**.

Two reasons to distrust the number until the splice-junction survival check measures it: the alignment graph retains
introns and intergenic sequence, so it is larger in nodes and edges than the genic graph all
the prune scaling (2.110-2.167 GiB peak per GiB of graph) was fitted on; and no run has ever
pruned a path-light graph with a transcript guide, so that regime is unmeasured.

## Decisions taken (2026-09-20, by the project owner)

**1. rpvg is out of scope for now.** The XG requirement that blocked booking the XG-to-GBZ
saving is therefore lifted. Note also that an XG is cheap to derive from a GBZ, so the
choice of `-x` format is no longer a pipeline constraint but a per-stage convenience. The
measured 54:28.44 at 68.52 GiB for `vg index -x` was taken on a **path-embedded
PackedGraph**; it is not the cost of converting a path-light graph or a GBZ, and should not
be quoted as such.

**2. The alignment graph must retain introns, exons, and at least 1 kb either side of every
transcript.** This is already what the existing design produces, and it is produced *with*
`-d/--remove-non-gene`, not by omitting it. `make_retention_features.py` emits, for every
transcript-body record, **one single-exon dummy feature covering the body plus the flank**,
clamped to the GBZ path fragment -- deliberately contiguous, and so "unable to introduce a
splice edge". Because that feature is contiguous rather than spliced, it marks every intron
node and every node within the flank as transcribed, and `-d` then removes only sequence
outside body-plus-flank. chr2 carries 7,475,429 exon Parents alongside 7,476,744 body
Parents, which is the two-layer annotation this depends on.

Two consequences. The alignment graph is the **genic-plus-flank** object that every prior
chr2 and chr21 measurement was made on, so those resource figures **do** transfer -- this
reverses a caution stated earlier in this document's history. And the retention pads are
load-bearing for intron retention, not merely for flanks: dropping them would silently
delete every intron.

**3. Testing is per-chromosome, but the GAMP and the node-ID space must both be joint.**
Per-chromosome indexes are a testing convenience only. Production requires one node-ID space
and one GAMP, because MAPQ is computed against the candidate set the mapper can see and
per-chromosome GAMPs are therefore not comparable or concatenable.

### The joint node-ID plan this forces

Ordering matters, and getting it wrong reproduces the stale-GBWT failure recorded against
`vg rna -v`. `sort_compact_nodes` renumbers from 1 per chromosome, and even with `-o` the
split-node IDs start at each chunk's own max, so per-chromosome `vg rna` outputs collide.

Two orderings are viable and the choice is **not settled**. The I/O gap between them is
large, so cost it before committing.

**Join late** -- offset the `vg rna` outputs. This is what the existing whole-genome driver
encodes, and it has no unknowns:

1. **`vg rna` per chromosome**, producing per-chromosome graphs in their own ID spaces.
2. **Join arithmetically.** `VGset::merge_id_space` (`src/vg_set.cpp:59-69`) is a running
   maximum plus `increment_node_ids`, and `vg ids -i N graph` applies exactly that same
   increment to one graph, reading and writing in a single pass. Every offset is a pure
   function of the per-chromosome (min, max) ranges, so the joint graph is re-derivable from
   the per-chromosome bank plus a recorded offset table.
3. **Build the guide GBWT after the join, from the joined graphs.** This is the step that
   makes the plan safe: a guide emitted by `vg rna` before the join carries pre-join node
   IDs and would be stale in exactly the way `-v` is stale under `-j`. Building it after the
   join means its IDs are correct by construction rather than by verification.
4. **Prune per chromosome with `-a/--append-mapping`**, accumulating into one
   `gcsa::NodeMapping`. Per chromosome `-a` is mandatory, not optional: without it each
   chromosome mints unfolded duplicate IDs from its own maximum and collides with the real
   IDs of every chromosome above it.
5. **One GCSA2** over all pruned graphs, on the external-memory route. This is the single
   joint stage, and it is what the fork's external-memory GCSA2 work exists to make
   tractable.

**Join early** -- offset the per-chromosome GBZ inputs *before* `vg rna`, so everything
downstream is born in joint IDs and no large join happens at all. The corpus joined is then
the GBZ set (order of tens of GB) rather than the path-embedded outputs (~2.10 TB), roughly
two orders of magnitude less I/O. The cost is that it needs **reserved per-chromosome ID
headroom**: `sort_compact_nodes` renumbers from 1 per chromosome, and with `-o` the split
nodes `vg rna` mints at exon boundaries start at that chunk's own maximum, which would run
into the next chromosome's range. Sizing that headroom needs the mint rate, which the chr21
check run reports for free.

Neither ordering has been measured. Join-late costs roughly 4.2 TB of streamed I/O and
~2.1 TB of transient disk against 9.5 TB free -- affordable and fully understood. Join-early
is far cheaper but carries an unproven headroom plan. **Decide after the chr21 check run**,
which supplies the mint rate either way.

One constraint binds both orderings: there is **no way to renumber an existing GBWT**.
`vg gbwt` exposes no remap, renumber or offset option, so the graph must carry its final
node IDs before the guide is built. `vg gbwt -f/--fast` does merge GBWTs whose node IDs do
not overlap, which an offset plan produces, so per-chromosome guides can be built
concurrently and fast-merged rather than built in one serial pass over a joint graph.

Memory binds the schedule rather than time: chr2's prune peaked at 341.07 GiB of a 1,007 GiB
host, so large chromosomes must not prune concurrently. The 24-way `-a` chain is unproven at
scale and should carry a mapping-header check around every prune.

## Do not do

- Do not chase the overlay cost through naming or tagging. `RECEIPTS.md` section 15 bounds
  what either can reach at 24.9% of indexed paths.
- Do not route a pad-carrying graph through `vg autoindex` or `vg prune -r`.
  `PhaseUnfolder::restore_paths` (`src/phase_unfolder.cpp:73-75`) filters to
  `{GENERIC, REFERENCE}` and silently drops HAPLOTYPE-covered nodes at exit 0.
- Do not run chr2, and do not run GCSA2 at any scale, until both checks pass.

## Research, but do not block on it

panCollapse's position query over a GBWT remains unsolved: `GBWTGraph` is not a
`PathPositionHandleGraph`, `PackedPositionOverlay` over all paths is O(total steps), and
thread-walking is refuted at 1,645.12 mean hops. The structural idea worth exploring is that
exact-dedup transcripts are near-identical and therefore prefix-share their positions, so a
prefix-shared position structure could be far smaller than O(total steps). That is research.
The route available today is to keep panCollapse on an XG of the counting graph, which the
single-run design above produces anyway.
