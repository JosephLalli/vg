# Proposal: per-chromosome GBZ plus CAT GFF3 to mpmap-ready indexes

Status: **proposal under critique, nothing here has been run end to end.** Drafted
2026-09-20 against the measured evidence in `RECEIPTS.md` and the verified survey in
`GBZ_INDEXING_SURVEY.md`, and building on the prior recipe at
`/mnt/ssd/lalli/hprc_v2_vg_rna/recipe_gbz_mpmap_annotation.md`, whose junctions-only
insight this pipeline adopts.

Pinned binary for every measured figure quoted:
`4f495d705c5547a39d1334a9c6cd7d4e02ece9e50fe65831ea79ba2679b4273c`.

## What goes in and what comes out

**In:** a per-chromosome `chrN.gbz`, extracted with
`vg chunk -t T --gbz -x whole.gbz -C --contig chrN -b OUT/chrN` (use `--contig`; `-P` is
silently a no-op in GBZ mode and builds all ~25 components), plus CAT-projected per-sample
GFF3 annotations.

**Out:** the three artifacts `vg mpmap` consumes -- a graph for `-x`, `spliced.gcsa` with
its `.lcp` for `-g`, and `spliced.dist` for `-d`. Of these only `-x` and `-g` are hard
requirements (`src/subcommand/mpmap_main.cpp:1630`, `:1634`); `-d` is optional but mpmap
warns it is "HIGHLY recommended" on splice graphs, and supplying it also skips the
component-labeling startup pass gated at `:2013-2016`.

## Step 1 -- accession-to-PanSN map, derived from the graph

    vg paths --list -x chrN.gbz > paths.txt
    python3 make_acc2root.py --paths paths.txt --out acc2root.tsv

Annotation seqids are bare accessions; the graph names paths PanSN. Build the map from the
graph rather than from an assumed naming convention, and fail loudly on ambiguity -- only
reference chromosome names should ever be ambiguous.

## Step 2 -- stream-rewrite the annotations

Per file, to stdout: rewrite the seqid to the PanSN root; keep only `exon` features; drop
records whose strand is not `+` or `-`; and prefix each `Parent=` with the PanSN
`SAMPLE#hap`. Do not materialize the result.

Each clause exists because of a recorded failure: `vg rna` aborts with
`Chromosome path "..." not found` on unrewritten seqids, SIGABRTs on a `.` or `?` strand,
refuses gzipped input outright, and -- the correctness-critical one -- CAT transcript IDs
of the form `SAMPLE_ha_T####` collide between a sample's two haplotypes, so without the
prefix the two haplotypes are silently conflated.

## Step 3 -- exact deduplication

Group byte-identical transcripts and keep one representative per group. This is hashing,
not pairwise alignment: chr2's exact selection over 14,349,975 rows took **1:39:18**.
Exact is the cheap rule to *compute*; its cost falls downstream, and on the counting graph
rather than this one.

## Step 4 -- `vg rna`, junction edges only

    vg rna -t 24 -z -j -s Parent -y exon --progress \
      -v spliced.hap.gbwt \
      -n <( stream_rewritten_gffs ) \
      chrN.gbz > spliced.pg

`-z` reads the GBZ directly. `-j` declares the annotations already sit on haplotypes.
**`-a` and `-r` are omitted deliberately**: for alignment the graph needs splice-junction
*edges*, and embedded transcript paths are a counting-graph concern. `-v` emits the
haplotype GBWT whose node IDs match `spliced.pg`; the input GBZ's own GBWT has pre-split
node IDs that will not align to the spliced graph, and substituting it is a silent
corruption rather than an error.

## Step 5 -- haplotype-aware prune

    vg prune -u -g spliced.hap.gbwt -m spliced.mapping -k 32 -t 24 spliced.pg > pruned.vg

Prune is **not** removable by any GBZ route. A GBZ drops only the edges no thread
traverses -- measured at 25,251 of 2,726,485, or **0.926%**, on chr21 -- which is nowhere
near the k-mer complexity bound GCSA2 requires.

## Step 6 -- GCSA2 on the external-memory route

    vg index -g spliced.gcsa -f spliced.mapping -t 24 \
      --gcsa-work-dir $TMPDIR/gcsa --gcsa-memory-limit 96G \
      --gcsa-sort-run-size 32G --gcsa-join-partition-size 32G --gcsa-process-workers 6 \
      pruned.vg

`-f` reconciles `prune -u`'s unfolded duplicate IDs back to the original node space, which
is what makes the resulting index usable against a graph built from the unpruned input.
Prefer this to `vg autoindex --workflow mpmap`, whose `--target-mem` is a hard k-mer-path
budget that silently re-prunes and restarts GCSA2 (observed: three restarts, 706 GB temp).

## Step 7 -- distance index through a GBZ of the spliced graph

    vg gbwt -x spliced.pg -g spliced.gbz spliced.hap.gbwt
    vg index -j spliced.dist spliced.gbz

Use the positional form: `-x` is hard-typed to `xg::XG` at
`src/subcommand/index_main.cpp:778`, so `-j out.dist -x graph.gbz` fails. The distance
index never needed an XG at all -- `fill_in_distance_index` takes a bare
`const HandleGraph*` (`src/snarl_distance_index.hpp:34`).

Measured arm-to-arm on chr21, same binary and options: from the GBZ **2:31.32 at
8.06 GiB**; from the XG **3:45.47 at 60.59 GiB**. That is 7.5x less peak memory and 1.49x
less wall, from one run per arm on a loaded host with **no noise floor** -- the memory
ratio is far outside plausible contention, the wall ratio is not.

## Step 8 -- choose the `-x` graph, then smoke-test

Either `spliced.gbz` from step 7 or `vg index -x spliced.xg spliced.pg`. On chr21's
path-embedded graph the two cost 3:30.59 at 46.35 GiB for 622,882,936 bytes against
54:28.44 at 68.52 GiB for 55,013,545,597 bytes -- 88.3x smaller and 15.5x less wall for
the GBZ. Whether that ratio survives on a junctions-only graph is unmeasured.

    vg mpmap -n rna -x spliced.gbz -g spliced.gcsa -d spliced.dist \
      -f reads.fq -F GAMP | vg view -a - | head

Do not declare the index done on exit status. `ref_path_handles`
(`src/subcommand/mpmap_main.cpp:1845-1857`) selects the longest path per connected
component without consulting path sense, so a graph with no true reference can yield a
silently degraded spliced alignment at exit 0.

## Known weaknesses, stated before critique

1. **Unverified premise.** Whether `spliced.pg` carries the input GBZ's haplotype paths as
   *embedded* paths is not established. If it does, mpmap's overlay cost returns -- measured
   at 14.3 minutes of a 27.2-minute startup on a 5.6M-path GBZ -- and step 8's whole
   rationale fails.
2. **Per-chromosome scope.** Mapping RNA-seq against one chromosome at a time breaks MAPQ
   and multimapping, and the resulting GAMPs cannot simply be concatenated. This pipeline
   inherits that from its input and does not solve it.
3. **Half the deliverable.** The panCollapse counting graph is a separate and heavier build
   (`-a -r -c all`; whole-genome observed at ~12 h and ~260 GB). This pipeline produces the
   alignment index only.
4. **Splice-strand context.** On a junctions-only graph it is unclear what mpmap adopts as
   the reference for splice-strand determination, given the `ref_path_handles` selection
   rule above.

## What this pipeline has never been run on

No stage of it has been executed end to end. The chr21 figures quoted are from a
*path-embedded* graph and a *transcript* guide GBWT, not from the junctions-only graph and
haplotype guide this pipeline builds. Every resource figure here is therefore an upper
bound of unknown tightness, not a prediction.

---

# Critique outcome: NO-GO as written (2026-09-20)

An adversarial review found three independent defects that each produce a silently wrong
index, all exiting 0 with no warning. Demonstrated on a 5-node fixture. The proposal above
is retained unedited as the object of the critique; do not run it.

## Fatal

**F1. `vg rna -v` does not emit a GBWT in the output graph's node space.** Two mechanisms,
both of which this proposal triggers. `src/subcommand/rna_main.cpp:590` passes
`!use_hap_ref` as `add_reference_transcripts`' `update_haplotypes` argument, so `-j` makes
it false and the guard at `src/transcriptome.cpp:2880` never fires -- exon-boundary node
divisions are never propagated into the GBWT. Independently, `sort_compact_nodes`
(`src/transcriptome.cpp:3631`) renumbers the graph via `apply_ordering` and updates only
`_transcript_paths`, never the haplotype index; sorting is on unless `-o` is given.

This is upstream v1.75.1 behaviour, not fork drift, and **the help text at
`rna_main.cpp:97-98` ("with node IDs matching the output graph") is false for the `-j`
route.** Verified independently at both line numbers.

On the fixture, a 9-node/9-edge spliced graph yielded a 5-node/4-edge GBZ through the `-j`
route. The most dangerous variant is `no -j, default sort`: right node count, right total
length, **scrambled sequence**. `vg prune -p -u -v/--verify-paths -g <stale gbwt>` exits 0
and reports verification complete, so the shipped gate does not catch any of this.

Consequence: steps 5, 6 and 7 all consume a corrupt input.

**F2. A GBZ built from a haplotype GBWT drops every splice junction.** A GBWTGraph's edges
are exactly those its threads support, and genomic haplotypes never skip an intron.
Fixture: 9 edges to 8, and the missing one is the junction. Step 7's GBZ is an *unspliced*
graph, so step 8 would map against a graph with no junctions while its GCSA2 has them, and
the distance index would describe the unspliced topology.

This also **narrows RECEIPTS.md section 12**: the 0.926% edge drop measured there is for a
*transcript* guide on a graph built with `-d/--remove-non-gene`, where every node is
transcribed. Symmetrically, a GBZ from a transcript guide drops every non-transcribed
*node* -- fixture: 9 nodes to 4. This proposal does not use `-d`, so that variant would
discard all intronic and intergenic sequence. **Neither GBZ is a valid mpmap `-x` here.**

**F3. Omitting `-a`/`-r` makes `vg prune` delete the splice junctions before GCSA2 sees
them.** In `-u` mode `prune_main.cpp:479-513` builds its XG from the remaining embedded
non-alt paths, `:524` removes complex edges, and `:551-566` restores only what the XG's
paths or the GBWT's threads traverse. With no transcript paths embedded and a haplotype
GBWT that does not skip introns, nothing protects the junctions -- and junction edges are
precisely the local branching `prune_complex_with_head_tail` targets. `vg autoindex` does
the opposite deliberately, embedding transcript paths before pruning
(`index_registry.cpp:3117`, `:3323`, `:3642`).

**This refutes the premise the whole design rests on.** The mechanism is source-certain;
the fraction of junctions lost at pangenome scale is unmeasured.

**F5. Step 8 has no graph to hand `-x`.** `spliced.xg` is named but never built.

## What the critique endorsed

Weakness (a) is **refuted, favourably**: `rna_main.cpp:462` copies only
`{PathSense::GENERIC, PathSense::REFERENCE}` out of the input GBZ, so the 462 haplotype
paths are *not* embedded and the overlay cost does not return. The count that matters is
REFERENCE+GENERIC in the actual `chrN.gbz`, settled by
`vg paths -x chrN.gbz -M | awk '{print $2}' | sort | uniq -c`.

Weakness (d) also resolves favourably for `spliced.pg`: with only the GBZ's reference
contigs embedded, the longest path per component *is* the chromosome reference.

## The finding that pays for the review -- and its retraction

The review proposed that step 2's `Parent=` prefixing with `SAMPLE#hap` causes the overlay
blowup recorded in `RECEIPTS.md`, and that a separator other than `#` would fix it at no
cost. **That is wrong, and was retracted on 2026-09-20 after a follow-up investigation.**

`get_sample_sense` (`deps/gbwtgraph/src/utils.cpp:174-188`) has three branches: the magic
generic sample yields GENERIC, a sample listed in `reference_samples` yields REFERENCE, and
everything else yields HAPLOTYPE. `PackedReferencePathOverlay` indexes REFERENCE **and**
GENERIC (`deps/libbdsg/bdsg/src/reference_path_overlay.cpp:29-31`). So removing the `#`
moves a transcript from REFERENCE to GENERIC, and the overlay indexes it either way.

The measurement that already disproved it was in hand: of chr21's 4,209,052 GENERIC paths,
2,803,513 are `__panSC_retention_pad1000__<sha>_L_R1` -- names with no `#` at all. They are
GENERIC *because* they lack PanSN structure, and they are what the overlay was traversing.

**The tag lever is also far weaker than stated.** Removing a sample from `reference_samples`
cannot move a path that has no sample field, so it reaches at most the 1,398,636
REFERENCE paths and leaves 4,209,052 GENERIC -- **75.1% of the 5,607,688 indexed paths** --
untouched. That matches the measured non-effect of `--set-reference CHM13`.

And the change is not representable where it would need to act: `vg rna` creates output
paths by name (`src/transcriptome.cpp:3804`), and the `_R1` copy suffix lands after the
PanSN phase-block field and breaks the regex, so transcripts emerge GENERIC in the `.pg`
regardless of any tag. The sense distinction exists only in the GBZ.

**Conclusion: the overlay cost has no cheap lever.** Only a rename that mints real PanSN
samples for the pads could move them, and that is a change to annotation naming with
consequences for panCollapse's identity ledger, not a free configuration tweak.

## Corrected direction

One `vg rna` invocation emitting both products in one node space:

    vg rna -z -j -g -r -c no -s Parent -y exon -b guide.gbwt ... chrN.gbz > full.pg

then derive the alignment graph by dropping embedded paths rather than building it a second
time. This keeps junctions protected through prune, keeps panCollapse on the XG route it
already validates, and makes the node-ID question structurally impossible rather than gated
on a check. `-c no` is required: `-c` defaults to `haplotype`, which the exact-rule arms did
not use. `-q/--out-exclude-ref` must not be used with `-j`: measured, it silently empties
the pantranscriptome.

## The one experiment authorized before anything else

A chr21 junctions-only run with `-v` replaced by `-g -b`, gated on two checks nothing in
the record has ever performed:

1. **Node-space gate.** Build a GBZ from the spliced graph plus the emitted guide and
   require the transcript sequences it yields to match `vg rna -f`'s FASTA as a name-sorted
   set. F1 fails this; a correct guide passes.
2. **Junction-survival gate.** Prune with the transcript guide, then count how many of
   `vg rna`'s junction edges survive into `pruned.vg`. This is the measurement F3 demands.
   If survival is not ~100%, the embed-then-strip design is mandatory rather than optional.

Nothing touching chr2, and no GCSA2 at any scale, until both gates pass.
