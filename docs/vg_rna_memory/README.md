# vg rna memory patches (Stage B)

Reviewable patches for the top opportunities identified in Stage A of the
`vg rna` memory investigation. Nothing here has been applied to `src/`, built,
or run. The diffs were produced against copies of the HEAD files, verified to
apply cleanly in sequence with both `git apply` (in a throwaway worktree) and
`patch -p1`, and desk-checked line by line; they have **not been compiled**.
Section 6 is the protocol for establishing that they build, produce identical
output, and save what they are expected to save.

Provenance: fork checkout HEAD `6ffdcb969` (`v0.11-16-g6ffdcb969`), the commit
the pinned production binary was built from. File hashes at that commit:

| file | sha256 (first 16) |
|---|---|
| `src/transcriptome.hpp` | `191ea83c2eae7436` |
| `src/transcriptome.cpp` | `fc797fcf354f73a4` |
| `src/path.cpp` | `5d04fade3e197d08` |

## 1. Contents and how to apply

| patch | opportunity | files | applies to |
|---|---|---|---|
| `0001-compact-edited-transcript-path-records.patch` | 1: 16-byte step records instead of protobuf Mappings | `transcriptome.hpp`, `transcriptome.cpp` | HEAD |
| `0002-augment-graph-lifetimes.patch` | 2: free translations and drain the edited list inside `augment_graph` | `transcriptome.hpp`, `transcriptome.cpp` | on top of 0001 |
| `0003-bypass-augment-for-exon-boundaries.patch` | 3: divide nodes without `vg::augment` | `transcriptome.cpp` | on top of 0002 |
| `0004-path-reverse-complement-no-empty-string.patch` | 4: no empty `std::string` per reverse-strand mapping | `path.cpp` | HEAD, independent |

0001-0003 are a series and must be applied in order. 0004 is standalone; it
touches a different file and is subsumed by 0001 for `vg rna` (after 0001,
`vg rna` no longer calls the function it guards), so it only matters if 0001
is rejected or for other callers of `reverse_complement_mapping_in_place`.

```bash
cd /mnt/ssd/lalli/.codex/worktrees/e3e42ac4-cc4f-4bc9-850a-591d197313b9/vg-latest
git apply --check docs/vg_rna_memory/0001-*.patch      # dry run
git apply docs/vg_rna_memory/0001-*.patch
git apply docs/vg_rna_memory/0002-*.patch
git apply docs/vg_rna_memory/0003-*.patch
git apply docs/vg_rna_memory/0004-*.patch              # optional
```

Do this in a separate worktree, not in the production checkout, so that
`bin/vg` in the production checkout stays the pinned
`v0.11-16-g6ffdcb969` binary (sha256 `4e63c323...`). Build with
`JOBS=24 ./build-local.sh bin/vg` from that worktree's root, never a bare
`make`.

Opportunities 5 and 6 from Stage A are not patched separately. 5 (the by-value
`Mapping` copy in `has_novel_exon_boundaries`) disappears with 0001, which
rewrites that function over the new record. 6 (the serial wall-clock tails:
`embed_transcript_paths`, the per-step completed-path rewrite, destruction of
the protobuf list) is only partly touched: 0001 removes the destruction cost
and 0003 removes most of augment's time, but `embed_transcript_paths` (221 s
on chrX, 1229 s on chr2) is untouched and would need a change to
`PackedGraph::append_step` batching, which is a different piece of work.

## 2. What Stage A measured, in the terms the patches use

All numbers below that are marked measured come from
`tmp/rna_profile_chrY_20260910/` (chrY, production recipe `-t 32 -p -z -j -c
no -s Parent -y exon -r -v ... -i ... -n gff3 gbz`, pinned binary): `time.txt`
(max RSS 5,367,492 KiB = 5.37 GB, 42.8 s wall), `stderr.ts` (phase timings),
`rss.tsv` (0.25 s RSS timeline), `heap/jeprof.675094.30.i30.heap` (near-peak
jemalloc dump, 4.87 GB in use), and `gdbrun/gdb.out` (45 stack samples).

Measured on chrY:

- The peak is inside `Transcriptome::add_reference_transcripts`, in the
  window between "Constructed 127857 reference transcript paths" and "Updated
  graph with reference transcript paths" (stderr.ts 1789079330.7 to
  1789079346.6; rss.tsv climbs from 2.7 to 5.15 GiB in that window and falls
  to 3.0-3.2 GiB during sort/compact and 2.5-2.7 GiB during path embedding).
- Of the 4.87 GB in use at the near-peak dump, 46.5% (2.26 GB) is the
  `list<EditedTranscriptPath>` and its protobuf `Path`s, 45.9% (2.24 GB) is
  the `vector<Translation>` that `vg::augment`'s `make_translation` builds
  (two Translations per graph node, 2.23M nodes), and everything else (graph,
  GBWT, transcripts) is under 8% (0.37 GB).
- Per step, the protobuf representation costs about 190 B: `Mapping` (64 B) +
  `Position` (48 B) + `Edit` (40 B) + the edit `RepeatedPtrField` header and
  pointer slot + a 32 B empty `std::string` that
  `reverse_complement_mapping_in_place` allocates through
  `mutable_sequence()` on reverse-strand steps (42% of mappings).
  Cross-check: chrY has 19,067,749 final path steps and about 0.63x that many
  pre-split edited steps (12.0M); 12.0M x 190 B = 2.28 GB, against 2.26 GB
  attributed.
- Time in the transcripts phase (18.9 s): parse 0.8 s, threaded construction
  0.35 s, `augment` about 13 s (of which `make_translation`'s sort and
  reverse-complement copies about 8 s), completed-path rewrite plus splice
  edges about 1.5 s, destroying the protobuf list about 1.3 s.

Projected (Stage A, `whole_genome_projection.tsv`): chr1 has 2.53G final
steps and an input chunk of 11.76M nodes, giving a transcripts-phase peak of
329-481 GB at 130-190 B per step, of which roughly 300-380 GB is the edited
path term and about 12 GB is the translations. chrX (493M steps, 86.5 GB
measured) and chr2 (439 GB, pre-copy-fix, genic recipe) anchor the band.

## 3. Patch 0001: compact edited transcript path records

**What it changes.** `EditedTranscriptPath::path` becomes
`vector<EditedMapping>`, where

```cpp
struct EditedMapping { handle_t handle; uint32_t offset; uint32_t length; };
```

is 16 bytes and holds exactly what every consumer read back from the old
`Mapping`: node, orientation, offset on that strand, and match length (the
old `Edit` always had `from_length == to_length` and an empty sequence). The
three producers (embedded-path route `project_transcript_embedded`, GBWT
route `construct_reference_transcript_paths_gbwt_callback`, projection route
`project_transcript_gbwt`) emplace records instead of protobuf mappings;
in-place reverse complement becomes a 10-line helper
(`offset' = node_length - offset - length`, `handle' = flip(handle)`, reverse
the vector), which is what `reverse_complement_path_in_place` reduced to for
single-match mappings. Consumers (`get_first_node_handle`, the
`CompletedTranscriptPath` constructor, `has_novel_exon_boundaries`,
`add_splice_junction_edges`, the completed-path rewrite in `augment_graph`,
path equality in `remove_redundant_transcript_paths`) read the fields
directly. The only place that still needs a protobuf `Path` is the handoff of
exon boundaries to `vg::augment`, so a helper `append_edited_mapping`
materialises the old `Mapping` (position, one full-match edit, rank) for
those transient one-mapping paths; the dedupe set becomes
`sparse_hash_set<EditedMapping, EditedMappingHash>` keyed on the same three
fields the old `MappingHash` hashed (node id, offset, orientation, edit
length).

**Expected saving.** Mechanism: 190 B to 16 B per step, with no per-step
heap allocation. `std::vector` growth can leave up to 2x slack, so the bound
is 16-32 B per step.

- chrY (estimate from the measured breakdown): the edited term drops from
  2.26 GB to 0.19-0.38 GB. The translations (2.24 GB) are still alive at the
  same moment, so the in-use peak goes from 4.87 GB to about 2.8-3.0 GB and
  max RSS from 5.37 GB to roughly 3.1-3.3 GB. A saving of about 40%, not the
  full 46.5%, because the other half of the peak is untouched by this patch.
- chr1 (projection, not measured): the edited term drops from about 300-380
  GB to 26-51 GB; with the translations (about 12 GB) and the completed
  handle vectors (2.53G x 8 B = 20 GB) still stacking as before, the
  transcripts-phase peak becomes roughly 60-100 GB plus graph, GBWT and
  annotation (of order 10 GB; chrY's 0.37 GB "everything else" does not
  scale linearly and was not measured at chr1 scale). Against 329-481 GB.

**Time.** Fewer allocations during construction and rewrite and no
destruction of a multi-million-object protobuf list (1.3 s on chrY, minutes
on chr1 by extrapolation). Not quantified; wall clock on this machine has a
20% contention spread (WORKSPACE_STATE.md, section 5), so any time claim
needs a repeated measurement.

**Risk: medium.** About ten functions change, and the patch is uncompiled.
Behavioural equivalence argument:

- Node, orientation, offset and length semantics are unchanged; `handle_t`
  is what `mapping_to_handle` reconstructed from (node id, is_reverse) at
  every read, and `flip` is what toggling `is_reverse` did.
- The protobuf `rank` field is dropped. It was set to the mapping's 1-based
  position at insertion and swapped along with the mapping on reverse
  complement, so it was a deterministic function of the path's content and
  strand. It influenced exactly one consumer: `operator==(Path, Path)` via
  `MessageDifferencer` in `remove_redundant_transcript_paths`, which now
  compares the record vectors. Two paths with identical records could differ
  in rank pattern only if one came from a '+' transcript and the other from a
  '-' transcript with the same node walk after reverse complement, which
  under `path_collapse_type == "haplotype"` requires the same transcript name
  (impossible: one transcript has one strand) and under `"all"` requires two
  different transcripts on opposite strands walking the same nodes in
  opposite directions on two haplotypes that contain each other's reverse
  complement. The production recipe uses `-c no`, under which paths are never
  compared. Ranks also reach `vg::augment` on the intron route, where
  `simplify()` re-ranks before anything reads them.
- `offset` and `length` are `uint32_t`; the old code computed them in
  `int32_t` locals before storing them in the (int64) protobuf fields, so no
  node length that worked before stops working.
- `MappingHash`, `operator==(Mapping)`/`operator==(Path)` and the
  `message_differencer.h` include are left in place; the two operators are
  now unused free functions (0003 removes the last `Mapping` comparison).

Coverage: the Catch2 `[transcriptome]` case exercises all three producers,
including the production `-j` route
(`add_reference_transcripts(..., haplotype_index, true, false)` at
`src/unittest/transcriptome.cpp:697-799`), the GBWT-update route
(`false, true` at line 606) and all three collapse modes. It does not cover
the intron route (`add_intron_splice_junctions`); nothing in the repository
does (there is no `vg rna` integration test under `test/t/`).

## 4. Patch 0002: lifetimes inside `augment_graph`

**What it changes.** Three lifetime fixes in `Transcriptome::augment_graph`,
each byte-identical by construction:

1. `vector<Translation>().swap(translations)` immediately after
   `translation_index` is built and sorted. The translations (two protobuf
   Translations per graph node, ~1 KB per node) were otherwise alive until
   the function returned, across the GBWT update and the rewrite of every
   transcript path.
2. `vector<Path>().swap(exon_boundary_paths)` right after `augment()`
   returns (one transient protobuf `Path` per unique boundary).
3. `augment_graph` takes the edited list by non-const reference and erases
   each `EditedTranscriptPath` as soon as its `CompletedTranscriptPath` has
   been built, so the completed handle vectors (8 B per step of the
   augmented path) grow while the edited records shrink instead of stacking
   on top of them. Both callers (`add_reference_transcripts`,
   `add_intron_splice_junctions`) own a local list they never read again;
   the pointer index built during construction is already out of scope.

**Expected saving.**

- chrY (estimate): small. The translations are the peak *while they are
  being built* inside `make_translation`, and this patch cannot move that;
  it only stops them outliving their use. Expect max RSS within 0.2 GB of
  the 0001 result. This is the honest reason 0002 is a low-value patch on
  small chromosomes and a real one on large ones.
- chr1 (projection): translations ~12 GB no longer stack on the rewrite
  loop, and the completed vectors (20 GB) cross over with the compact edited
  records (26-51 GB) instead of adding to them; together roughly 30-40 GB
  off the 0001 figure, consistent with Stage A's 35-45 GB (Stage A's figure
  was against the unpatched 190 B/step list, where draining saves the full
  20 GB rather than the overlap).

**Risk: low.** The erase-as-you-go loop is the standard
`it = list.erase(it)` idiom over a `std::list`, order preserving; the
reference taken from `*it` is used only before the erase. The signature
change is private to `Transcriptome`.

## 5. Patch 0003: divide nodes at exon boundaries without `vg::augment`

**What it changes.** `augment_graph` no longer calls `augment()`. It keeps
augment's first pass exactly: for each boundary path, in the same order and
with the same `break_ends` (`true` for transcripts, `false` for introns), it
calls the same `find_breakpoints(simplify(path), ...)`, then the same
`forwardize_breakpoints` and `ensure_breakpoints` (all public in
`src/augment.hpp`). `ensure_breakpoints` is what divides the nodes, left to
right per node, iterating the same `unordered_map` built by the same
insertion sequence, so every `divide_handle` call happens in the same order
and every new node gets the same id as before. Its return value,
`map<pos_t, id_t>` from old position to piece id on both strands, is then
turned directly into `translation_index`:

- skip the offset-`L` sentinels (`id 0`);
- skip entries where `offset == 0 && piece == original id` (the leftmost
  piece on the forward strand translates to itself; this is precisely what
  `from_mapping != to_mapping` filtered out of the translations);
- otherwise `translation_index[get_handle(id, is_rev)] += (offset,
  get_handle(piece, is_rev))`, then sort per handle as before.

Dropped: augment's second pass (`add_nodes_and_edges` per boundary path, the
`embedded` Path copies, the edge check between consecutive mappings), the
walk over every step of every embedded path calling `create_edge`,
`orig_node_sizes` (one entry per graph node), `make_translation` (2N
protobuf Translations, a `std::sort` of them with a protobuf comparator, N
`reverse_complement_path` + `simplify` copies), and the OpenMP index build
with its 2N `MessageDifferencer` comparisons.

**Why the graph is the same.** On the transcript route every boundary path
is one full-match mapping. In `add_nodes_and_edges` a single match edit
starts with an empty `dangling` set, so no dangler edges are created; the
subsequent loop creates edges only between consecutive pieces of the same
original node, which `divide_handle` already connected, and
`PackedGraph::create_edge` looks the edge up first and returns if it exists
(`base_packed_graph.hpp:1236-1246`). The path-step walk creates an edge only
where an embedded path follows a missing edge, which a valid input graph
does not have. So the edge set is unchanged, and no edge insertion happens
that could reorder PackedGraph's adjacency lists. `translation_index` is
built from the same facts (`ensure_breakpoints`' map is what
`make_translation` inverted and reverse-complemented to produce the
translations; the reverse-strand offset `L - end_i` and the last-piece entry
at reverse offset 0 correspond exactly, and the "only store changes" filter
is reproduced as stated above).

**Expected saving.**

- chrY (estimate): the translations term (2.24 GB in use, 45.9%) is gone
  entirely rather than freed early. With 0001+0002 the transcripts-phase
  in-use peak becomes graph + GBWT + records + breakpoints, about 0.6-1.0 GB.
  Max RSS will not fall that far: `rss.tsv` shows 2.5-3.2 GiB resident in the
  later phases today, some of which is memory jemalloc retained after the
  old peak and some of which is live PackedGraph path storage for 19M
  embedded steps. Expect max RSS somewhere between 1 and 3 GB; the probe
  in section 6 measures it.
- chr1 (projection): about 12 GB (translations) plus `orig_node_sizes`
  (11.76M entries, ~0.6 GB) plus the transient one-`Path`-per-boundary
  vector; roughly 12-15 GB off the 0001+0002 figure, leaving a
  transcripts-phase estimate of about 40-65 GB plus graph/GBWT/annotation.
  After all three patches the whole-run peak is probably no longer in this
  phase at all but in `embed_transcript_paths` (PackedGraph storage for
  2.53G steps) with `_transcript_paths` (20 GB) still alive; Stage A could
  not see those phases because `gcsa::memoryUsage()` reports max RSS.
- Time: most of augment's ~13 s of the 18.9 s transcripts phase on chrY
  (make_translation ~8 s, plus the second pass, the path-step walk and 2N
  MessageDifferencer calls). Extrapolated, not measured, for chrX (240 s
  phase) and chr1.

**Risk: medium**, for these reasons:

- Uncompiled, and the equivalence argument above is reasoning about
  `augment.cpp`, not a measurement. The `-t 1` byte gate in section 6 is the
  test of that reasoning; a mismatch there with identical `-t 1` output
  under 0001+0002 would localise the fault to this patch.
- Intron route (`vg rna -m`): augment's second pass used to create each
  intron's splice-junction edge during the second pass;
  `add_splice_junction_edges(updated_transcript_paths)` creates the same
  edges later. The edge *set* is identical, but the insertion *order*
  relative to other edges differs, which can change PackedGraph's adjacency
  list layout and therefore `.pg` bytes without changing the graph. The
  production recipe does not use `-m`, and no test in the repository
  exercises it. If the intron route matters, gate it with a canonical text
  form (`vg convert -f` with sorted lines) rather than raw bytes.
- The path-step edge walk is gone. If an input chunk had an embedded path
  following a missing edge, the old code silently added it and the new code
  does not. `vg validate` on the input catches this (11 of 25 canonical
  chunks are validated; chrY, chrX and chr3-9 are not, per
  WORKSPACE_STATE.md section 7), and so does the byte gate.
- `num_threads` is no longer used in `augment_graph` (the index is filled
  serially from a `std::map`; a few million entries on chr1, seconds).

## 6. Validation protocol

`vg rna` at `-t 32` and `-t 48` is not byte-reproducible for `.pg` and
`.gbwt` (established in commit `0d6a46873`'s A/B and in WORKSPACE_STATE.md
section 5; peak RSS reproduces to 0.03%). The mechanism is visible in the
code: construction threads splice their path lists in completion order, that
order fixes the order of `exon_boundary_paths`, which fixes the
`unordered_map` insertion order in `find_breakpoints`, which fixes the order
of `divide_handle` calls and therefore new node ids, which post-compaction
identity then inherits through `topological_order` tie-breaking. At `-t 1`
none of that is concurrent, so `-t 1` output should be deterministic, but
**this has not been measured**: a probe to establish it was prepared
(`chrY_validation.sh`, step 0) and not run, because launching `vg` was out
of scope for this stage. Step 0 must pass before steps 1-3 mean anything.

`chrY_validation.sh CONTROL_VG PATCHED_VG OUTDIR` in this directory runs the
whole protocol on chrY (the only chromosome with a banked current-recipe
product; 5.37 GB, about 45 s per run at `-t 32`, longer at `-t 1`). It was
written for this document and has not been executed; read it before running
it. It uses the same inputs and flags as the Stage A profile
(`tmp/rna_profile_chrY_20260910/run_profile.sh`) plus `-f` for a FASTA of
the transcript sequences.

0. **Reproducibility floor.** Run the control binary twice at `-t 1`.
   `.pg`, `-v` GBWT, `-i` info table and `-f` FASTA must be byte-identical.
   If they are not, the byte gate in step 1 is unusable and only the digests
   in step 2 remain; report that.
1. **Byte gate at `-t 1`.** Patched binary at `-t 1`: all four outputs must
   equal the control's. For 0001 and 0002 this is the expected result on
   both routes. For 0003 it is the expected result on the transcript route
   (the production recipe); see section 5 for the intron route.
2. **Digest gate at `-t 32`.** Patched and control at `-t 32`: compare (a)
   sha256 of the `-i` info table (already known to be thread-count
   invariant from the chr21 A/B), (b) sha256 of the `-f` FASTA after sorting
   records by name (path names and copy ids are assigned after a by-name
   sort, so this is relabel-invariant), (c) `vg stats -N -E -l -z` on the
   `.pg`, (d) the sorted multiset of node sequences (`vg convert -f` S
   lines, column 3, sorted). These are the checks that survive
   non-reproducible node numbering.
3. **RSS with a same-binary control.** `/usr/bin/time -v` on patched and
   control at `-t 32` in the same session; compare max RSS against each
   other and against the Stage A profile (5,367,492 KiB). The chr21 A/B
   measured a 0.03% run-to-run floor; anything under a few percent is
   noise. Expected per section 3-5: about -40% after 0001, about the same
   after 0002, and between 1 and 3 GB after 0003.
4. **Unit test.** `./bin/vg test "[transcriptome]"` (732 assertions at
   HEAD); covers all three producers and all collapse modes, not introns.
5. **Graph validity.** `vg validate` on the patched `.pg`.
6. **Wall clock**, if reported at all, needs at least three runs per arm
   because of the 20% contention spread on this machine.

Once chrY passes, the first large-chromosome run should be chrX (86.5 GB
measured today, 9 min wall, `wg_nohg002_k30_production_20260910T090632`),
which also tightens the 130-190 B/step band that the chr1 projection rests
on (Stage A open item).

## 7. What is measured and what is estimated

Measured (chrY, Stage A artifacts): the 46.5% / 45.9% split, the ~190
B/step, the phase timings, the 0.63 ratio of pre-split to final steps, the
post-phase RSS floor of 2.5-3.2 GiB. Established elsewhere: `-t 48`
non-reproducibility and the 0.03% RSS floor (chr21 A/B).

Estimated: every post-patch number in sections 3-5. They are arithmetic on
the measured breakdown, not measurements; chrY's peak is split evenly
between the two structures while chr1's is dominated by the edited paths, so
the patches' relative value differs between the two and neither has been
run. Also estimated: chr1's 2.53G steps and its 329-481 GB pre-patch peak
(Stage A projection with a 1.5x band), and everything said about time.

Not established: that the patches compile; `-t 1` reproducibility; the
whole-run peak after the patches (likely set by `embed_transcript_paths`,
which no patch here touches); the intron route under 0003.

## 8. Scratch material

`tmp/vg_rna_memory_work/` (untracked) holds the HEAD copies (`base/`), the
per-patch edited trees (`p1/` to `p4/`), the Python scripts that produced
them (`edit_p*.py`, `editlib.py`; edits are anchored on line content and
fail loudly on a mismatch), the `patch`-applied verification tree
(`verify/`, byte-identical to `p3/` plus `p4/`'s `path.cpp`), and the
unexecuted probe script `probe_t1/run_probe.sh`.

## 9. Measured results, 2026-09-10 (chrY, production recipe, pinned control 4e63c323)

| patches | -t 1 byte gate | -t 32 digests | unit test | validate | RSS patched/control | merged |
|---|---|---|---|---|---|---|
| 0001+0002 | identical (pg, prune_paths.gbwt, info.tsv, tx.fa) | all identical | pass, 732 assertions | valid | 3.03 / 5.19 GB = 0.584 | yes, at 583bfd90c |
| +0003 (unscoped) | identical | all identical | FAIL: unittest/transcriptome.cpp:842, edge count 15 != 17 (intron route) | valid | 1.98 / 5.19 GB = 0.381 | no, superseded |
| +0003 scoped to the transcript route | identical | all identical | pass, 732 assertions | valid | 1.94 / 5.06 GB = 0.383 | yes, at 28dc0b7be |

Step 0 established that vg rna output is byte-reproducible at -t 1 (two control
runs, identical .pg). Reports and /usr/bin/time -v files are under results/.
0003's failure is on the vg rna -m intron route, which the production recipe does
not use; the transcript route is byte-identical. The bypass is therefore gated on is_introns: the --transcripts route takes the
fast path, the --introns route still calls augment(). Reproducing augment's
intron edge semantics in the first pass remains unimplemented; vg rna -m keeps
the original memory profile.

## 10. chr20 and chr18, 2026-09-10: the patches at chromosome scale

chr20 was run as a full gate against the pre-patch control (v0.11-16-g6ffdcb969,
sha 4e63c323) with the merged binary (v0.11-24-g139102fa0, sha 35867c7f).

| run | peak RSS | wall | result |
|---|---|---|---|
| control -t 1 (a) | 105.32 GiB | 22:17 | reproducibility floor |
| control -t 1 (b) | 105.28 GiB | 22:38 | byte-identical to (a): floor holds on chr20 |
| patched -t 1 | 15.31 GiB | 15:07 | byte-identical to control: pg, prune_paths.gbwt, info.tsv, tx.fa |
| control -t 64 | 105.22 GiB | 15:24 | |
| patched -t 64 | 17.00 GiB | 11:04 | all relabel-invariant digests identical |

Controlled ratio 0.162 at -t 64 and 0.145 at -t 1, against a control-vs-control
floor of 0.04% on this chromosome. vg test [transcriptome] and vg validate pass.

chr18 (patched only, -t 64, no control arm): 11.83 GiB peak, 6:56 wall, graph
valid, 515,435 embedded paths against 515,431 transcripts parsed.

The composition of the remaining peak has changed, and so has what predicts it.
chr18 and chr20 have near-identical actionable-parent counts (20,421 vs 20,924)
but differ 11.83 vs 17.00 GiB at the same thread count, while their annotations
differ 66 vs 103 MB. Pre-patch, parent count predicted peak far better than
annotation size; post-patch the patches removed the term that scaled with
transcript steps, so annotation size now fits better. Any cap model refitted on
patched measurements should be treated as provisional until there are anchors
from a large chromosome.
