# Transcript-rich graph memory implementation

This follows the [completed attribution report](README.md). The active goal is
to reduce the cost of the same exact-only annotation, preserving every named
walk, graph topology, retained base, pruning protection, and node mapping.

## Scope correction (2026-09-20): counting-graph memory versus the mpmap alignment index

**Scope of this correction:** the full-chr21 RNA V3 parallel and serial
acceptances, the full-chr21 prune V2 and current-binary acceptances, and the
OR/exact-only acceptance-target table below all measure the same object -- the
**counting graph**, a PackedGraph with all 5,607,688 named transcript walks
embedded (the graph whose canonical fingerprint recurs throughout this file as
2,056,621 nodes / 2,726,485 edges / 5,607,688 named paths). That is the graph
panCollapse's per-node thread-multiplicity queries run against, and for that
purpose these figures remain exactly right and should keep being cited as the
cost of the graph panCollapse counts from. The smaller fixtures and synthetic
probes elsewhere in this ledger -- the 37-node prune fixture, the 432-path real
RNA fixture, the 50,003,968-step synthetic writer, the 96-character
path-name scaling probe -- are bounded stand-ins for pieces of that same
construction path on much smaller inputs; they are not restatements of the
counting graph's own cost, and this correction does not reclassify them.

**New evidence and a verified shared input:** measurements taken 2026-09-20 on
the pinned binary SHA256
`4f495d705c5547a39d1334a9c6cd7d4e02ece9e50fe65831ea79ba2679b4273c` -- the
binary this ledger's RNA metadata duplicate-lookup integration build produced
below (2026-09-16) -- separate the counting graph from the graph `vg mpmap`
actually needs to align against. Full receipts:
`docs/exact_dedup_indexing_feasibility/RECEIPTS.md`; adversarially verified
survey: `docs/exact_dedup_indexing_feasibility/GBZ_INDEXING_SURVEY.md`;
synthesis assessment drawing on both: `docs/exact_dedup_indexing_feasibility.md`.
Their input is `/mnt/ssd/lalli/hprc_v2_vg_rna/notes/evidence/chr21_exact_arm_20260914/exact/genic.pg`,
which this ledger's own `chr21_prune/control-input-output.sha256` receipt
already names as the retained exact genic input that fed the prune V2 and
current-binary acceptances below. A fresh hash of that file taken for this
update confirms it is unchanged: SHA256
`0678d83d0adfeda8acd99265b5f0e62d759116244877933552cc5fd6b66c263b`, matching
that receipt exactly. This confirms byte-identity for prune's input
specifically; it is not the RNA V3 acceptances' own output graph, which is a
separate ~38.1 GB artifact recorded below (38,126,287,944 bytes for parallel
V3, 38,148,975,648 bytes for the serial control) -- genic.pg is what the
retained exact RNA control (1:01:06 wall, below) produced and what prune then
consumed, not what the V3 runs themselves produced.

**What was measured, and what it is not:** on that verified input, `vg index
-x` -- a standalone, serialized XG build -- peaked at 68.52 GiB and took
54:28.44 wall, producing a 55,013,545,597-byte XG file. `vg gbwt -g` -- a
standalone GBZ build from the same PackedGraph and guide GBWT -- peaked at
46.35 GiB and took 3:30.59 wall, producing a 622,882,936-byte GBZ: 88.3x
smaller than that XG file (59.5x smaller than the counting-graph PackedGraph
itself) and 15.5x less wall than `vg index -x` (one run of each command,
`vg index -x` at `-t 24` against `vg gbwt -g`'s unspecified thread count, no
noise floor established; the size ratios are deterministic and need none).
This ledger already builds an
XG from this same input, but only in-process, inside `vg prune -u`: the V2
phase samples below record a 75.61 GiB peak during that XG-construction phase,
on the pinned `vg-xg-consume` binary `31598084...89e256`; the current-binary
prune acceptance is a separate run, on a later binary `545de451...805f53`, at
74.815071 GiB overall. Those numbers describe the whole pruning process' RSS
while XG construction runs, not a standalone XG artifact measured alone. The
2026-09-20 figures are, per the survey, the first standalone `vg index -x`
RSS measurement taken in this project at any scale, and they ran on a third
binary, `4f495d70...4273c`. Do not read 68.52 GiB against 74.815071/75.608 GiB
as a same-binary or same-command comparison; they are different commands --
one measured in isolation, one embedded in a larger process -- on three
different binaries.

**Do not book these figures as the cost of an mpmap index:** `vg mpmap`'s
alignment index does not need the full transcript-annotation walk set that
makes this ledger's counting graph expensive.
`hprc_v2_vg_rna/recipe_gbz_mpmap_annotation.md` step 3 already specified
building the alignment index from splice junctions only (omitting `-a`/`-r`);
the 2026-09-20 assessment (`docs/exact_dedup_indexing_feasibility.md`, drawing
on the receipts and survey above) confirms that recipe was right, and
separately records that an earlier whole-genome XG size projection had been
computed from the counting graph rather than the alignment graph -- exactly
the conflation this note exists to prevent. This ledger's RNA and prune peaks
are the cost of building and pruning the transcript-annotated graph that
panCollapse queries; the alignment-side index is a separate, and on the
measurements above, much smaller object on disk -- not necessarily in
resident memory, per the reference-sense caveat below.

**Open limits that qualify this correction:** no junction-only GBZ or XG has
been measured -- the 46.35/68.52 GiB figures above are for the
*full-annotation* artifacts, the same walk set as this ledger's counting
graph, not a stripped one -- so there is no measured floor yet for the
alignment index's actual size. Also, no shipped `vg` route builds a
correctly-sensed alignment-only GBZ: `--set-reference` cannot fix path sense,
because `get_sample_sense` assigns GENERIC by PanSN naming regardless of the
tag (`deps/gbwtgraph/src/utils.cpp:174-188`), and the one route that empties
the GENERIC bucket (`vg rna -b`) instead leaves `ref_path_handles` empty
(`mpmap_main.cpp:1835-1857`), a silent, exit-0 MAPQ 1->60 hazard, not a fix. Dropping
`--add-ref-paths`/`-r` to reach a junction-only input is verified only at
37-node fixture scale -- far below both this ledger's own 2,056,621-node chr21
counting graph and chr2's separate, 50,647,839-node production-pruned graph --
and the two arms of that fixture gave different duplicate node IDs, so
acceptance there still needs this ledger's own relabel-invariant digests, not
a raw comparison. This ledger's
own prune-memory saving comes specifically from releasing *embedded*
transcript paths after XG copies them (`on_input_consumed`, described below
under XG reverse occurrences); an `-r`-free, junction-only input has no such
path payload to release, so how much of that saving mechanism -- as opposed to
prune's memory requirement generally -- carries over to a junction-only input
is unmeasured, not assumed to transfer. Separately, GBZ does not remove
`vg prune` or GCSA2 from the mpmap pipeline at all
(`src/subcommand/mpmap_main.cpp:1633-1635` hard-exits without `-g`; all three
seeders are GCSA2 queries), so prune stays required for any mpmap route,
junction-only or not. No controlled `vg mpmap -x` XG-versus-GBZ runtime
comparison exists (`RECEIPTS.md` section 14 is a five-read smoke test whose
154.55 GiB peak is dominated by `ReferencePathOverlay` startup, not mapping).
And if `rpvg` stays in the downstream pipeline it requires an XG input
(`rpvg src/main.cpp:437`), so even the XG-to-GBZ saving above is deferred, not
banked, until that dependency is resolved. These are indexing-cost findings,
not annotation-policy or production-indexing conclusions: per this file's own
separate-efforts rule, a result in one authorizes nothing in the others.

## Runtime requirement and change of direction (2026-09-15)

The user rejected the multi-fold runtime cost of the mapped/spooled RNA route.
**Memory-only acceptance is insufficient.** The <2x-OR memory targets remain,
but whole-command runtime is a required acceptance criterion; the optional
1.5x stretch must also be practical. A smaller RSS does not justify an arbitrary
increase in wall time or I/O.

The retained exact RNA control completed in 1:01:06. The mapped RNA V4 run was
still embedding after five hours: construction/graph updating took 8,679.14
seconds versus 635.702, and sorting took 3,150.89 versus 193.284. These are
absolute timings under different host conditions, not controlled slowdown
estimates; the size of the regression and the user's runtime requirement rule
out promoting this candidate. The user subsequently requested that prune
continue, RNA stop, and RNA temporary files be cleaned up. RNA's full objective
now requires rework with runtime and memory considered together.

RNA V4 was intentionally stopped after 307.32 minutes at the last live sample;
its sampled high-water mark was 9,140,332 KiB. It never finished embedding or
produced a validated output, so this is not a completed benchmark. All four
owning PIDs are absent and the RNA cgroup is empty. The first immediate
quiescence check raced process exit; the retained follow-up proves retirement.
Prune's invocation, PID and memory/swap limits stayed unchanged.

Six disposable payloads (`graph.arena`, `edited.steps`, `completed.steps` in
each of `rna_spool/chr21-candidate-v2/workspace/` and
`rna_spool/chr21-candidate-v4/workspace/`) were removed, totaling 256.453 GiB
of allocated storage. Ownership, inode, link-count and accessible-process
file/mapping checks precede deletion; both RNA owning services were inactive.
Their directories now contain `SCRATCH_REMOVED.json` markers. Code, binaries,
logs, measurements, completed controls and every prune/GCSA workspace remain.
The cleanup supersedes historical scratch-retention statements below.
Receipts under `runtime_priority_20260915/`: `rna-stop-before.json`,
`rna-stopped.json`, `rna-quiescence-followup.json`,
`rna-cleanup-manifest.json` and `rna-cleanup-complete.json`.

The RNA V5 watcher has been intentionally disabled to prevent automatic
follow-up validation of the runtime-rejected candidate. Its old PID is absent,
the semantic validator was never launched, and both indexing-job invocations,
PIDs and memory/swap limits stayed unchanged. Receipts:
`runtime_priority_20260915/watcher-before.json` and `watcher-disabled.json` under
the task artifact root. This is an intentional orchestration change, not an
indexing failure; automatic follow-up validation must remain disabled.

The default-storage prune V2 result remains the useful completed result:
75.608 GiB, 4:14:01 wall, unchanged graph semantics and mapping, against the
retained exact control's 256.696 GiB and 4:16:41. This supports the memory gain
without a demonstrated large runtime penalty; it is not a controlled speedup.

### Frozen current-binary prune revalidation (2026-09-16 UTC)

A completion audit found that V2 proves the required `<2x OR` target only for
its pinned `vg-xg-consume` binary. The current delivery binary includes later XG
move/block-decoding and libbdsg changes; narrow fixtures cannot establish its
full-scale RSS. The ordinary PackedGraph revalidation at
`tmp/transcript_memory_20260915/prune_current_binary_v1/` therefore repeated
the V2 argv and mapping seed with no workspace flag, pinned to binary SHA256
`545de451...805f53` and its 574-entry source manifest. GNU time measures only
`vg prune`; semantic validation runs in a separately capped service.

The fixture service invocation `dece0c5939e34d8d8eb19124001a0e9c` is terminal
PASS. TAP 26 and ordinary T1/T4/T24 plus real T24 have exact graph/mapping bytes,
verification and validation; source/input guards stayed stable.

The full current-binary command invocation
`be246266875c49e48228747eebedad77` is terminal PASS. Its command receipt,
`prune_current_binary_v1/chr21/command-acceptance.json`, records 78,449,288 KiB
(74.815071 GiB, 1.784731532x OR), 3:21:53 wall, zero swap, graph SHA256
`4a46c83f68e6c9c38722e5da1c5d8d39e70656323bba2f108ea33c23b4a81476`, and
mapping SHA256
`15778a314ce058c0d25038ad2cf787d5c8a48ced37322d02195cde5d132786cd`.
It passes the strict `<2x` target and does not pass the `<=1.5x` stretch target.

The original fail-closed terminal receipt at
`prune_current_binary_v1/chr21-terminal/acceptance.json` retained a passed
pathless semantic model and `vg validate`, but failed because this unfamiliar raw
hash still required a pair-specific storage classification. The proof is retained
under `prune_current_binary_v1/chr21-terminal/storage-classification-v1/`.
`prune_current_binary_v1/check_terminal.py` now accepts this exact current graph
hash only when the proof hashes match; no storage exception was generalized to
other hashes. The rerun service
`vg-memory-prune-current-terminal-proof-v1-20260916.service`, invocation
`d87bccbdf53a428a99515f1afcf366d6`, is terminal PASS at
`prune_current_binary_v1/chr21-terminal-current-proof/acceptance.json`. Its
validation/provenance cgroup peaked at 24,425,545,728 bytes, below its 32 GiB
cap, with zero swap.

This closes current-binary delivery provenance for the required `<2x` prune
goal. It does not close the 1.5x target. The failed mapped V5 remains rejected,
and no speculative 1.5x run is authorized.

Both sides of that comparison already include the earlier pruning speed work:
concurrent unfolding (`bbf264574`), bulk path deletion (`409c30a77`), and bulk
edge deletion (`b47de4db9`). The running V5 binary reports `v0.11-27-g9a377ca9c`
and matches its retained SHA256 `b87e0672...`; the exact control reports
`v0.11-24-g139102fa0`. All three commits are ancestors of both revisions.
The active `rna-copy-elimination` checkout descends from the primary checkout's
`Fast-prune` tip, and the primary's dirty bulk-pruning code and tests match the
current files. No local or fork-remote branch is literally named `vg-local`.
Thus the similar four-hour timings concern the additional memory changes;
they do not measure or negate the earlier pruning speed improvements.
The live branch/binary audit is `runtime_priority_20260915/prune-provenance.json`.

Next speculative implementation work would have to prioritize eliminating copies,
compact/shared path storage that retains every named walk, and fewer traversals.
Any further disk use must demonstrate efficient I/O rather than relying on mapped
mutation and repeated flushing. Before another full-chromosome candidate launch,
require a bounded comparison of whole-command wall time, CPU time, I/O, peak RSS
and semantics with matched inputs, flags and threads. No new full-scale retry is
justified solely by passing a memory cap. The accepted V3 full chr21 RNA result
already satisfies the requested memory objective; later RNA metadata work is a
small integrated correctness-gated optimization, not a new full-RNA runtime
claim.

### RNA metadata duplicate-lookup integration (2026-09-16 UTC)

After the current-binary prune proof-backed terminal gate closed, the private
metadata lookup prototype was applied to the live tree. The patch removes the
outer `metadata.has_path(name)` checks from the serial and parallel
`serialize_with_paths` metadata loops, relying on `create_path_handle(name,
false)` to perform the same duplicate-name rejection before mutation. The source
changes are limited to `deps/libbdsg/bdsg/include/bdsg/internal/base_packed_graph.hpp`
and `src/unittest/packed_path_stream.cpp`; `integration/apply/acceptance.json`
records those two changed paths, 572 unchanged production sources, a stable live
library guard, and apply receipt SHA256
`5d7dbcfbe1076e98b5ff74e2aa62898bcb82b04942786d38d3c19b55d4158f14`.

The guarded build service
`vg-memory-rna-metadata-integration-build-v1-20260916.service` invocation
`1a9c4cf6b67f4410899b6558ad8a49b2` is terminal PASS. Its acceptance receipt
records integrated binary SHA256
`4f495d705c5547a39d1334a9c6cd7d4e02ece9e50fe65831ea79ba2679b4273c` and
`libbdsg.a` SHA256
`262d7dd0b8d864e523fb08b9e427aca76c41ffba9c99d8ef06b0e03a724f4d91`.

The guarded check service
`vg-memory-rna-metadata-integration-checks-v1-20260916.service` invocation
`3d32e35743a845c992afa34c8639e333` is terminal PASS. Its acceptance receipt
records linked metadata tests, unit success, stable sources, stable watched
inputs and stable live library guard. The T1 and T24 432-path real fixtures both
pass `vg validate`, exact accepted `vg info` bytes, and exact accepted canonical
fingerprints (197,036 nodes, 211,508 edges, 432 paths). T1 keeps exact graph
bytes. T24 has a different raw packed graph hash while retaining the same
validated graph semantics, consistent with the earlier V3 packed-history
finding. This closes integration correctness for this small metadata
optimization. It does not claim a full-chr21 RNA runtime improvement beyond the
private 5.6M-name CPU-only ABBA gate.

### Prune runtime opportunities (2026-09-15)

A bounded probe identifies repeated compressed-handle decoding as a concrete
runtime candidate. `XGPath::handle(j)` reads `sdsl::enc_vector<>` through
`operator[]`, which decodes the prefix from the nearest sample on every access.
Its sample density is 128. `index_node_to_path()` currently repeats that access
pattern in the count pass and the two orientation-ordered fill passes.
`get_inter_sampled_values()` plus `sample()` can decode a block once using a
128-entry (1 KiB) scratch buffer. Plain iterator syntax still uses random access
and would not remove this repeated work.

`docs/transcript_path_memory/xg_decode_probe.cpp` verified every value against the
existing accessor at empty/singleton/block-boundary sizes, for repeats,
nonmonotonic and wrapping values, and 256 fixed-seed random cases. Three scans of
one million jagged/repeated handles produced the same checksum
`10015426076754691122`: existing decoding took 0.931014 seconds, block decoding
0.024905 seconds (37.38x for this operation). The combined process peaked at
10,240 KiB, used no swap and exited zero in 1.08 seconds under a 2 GiB/60-second
bound. Receipts and exact commands are in `xg_decode_probe_v1/` under the task
root. This is a decoding microbenchmark, not a whole-XG/prune speedup. The live
V5 run is unchanged.

A second source-backed opportunity is a specialized all-path deletion operation.
The existing bulk `destroy_paths()` still visits every selected step and each
visited node's membership list, even when no paths will remain. Directly clearing
path storage could avoid those visits, subject to graph metadata/counter and
serialization validation. The retained original-graph-to-paths-removed interval
was 47.0 minutes in V2 and 113.9 minutes in V5; it includes XG path copying as
well as deletion, so it does not isolate deletion's cost. V2 then spent 99.8
minutes between paths-removed and temporary-XG-complete. V5's corresponding
interval remains unfinished. Do not label the combined interval a measured
all-path deletion time or extrapolate the decoding microbenchmark to it.

### Shared source-walk RNA rework

Start with the GBWT reference route's edited-path representation. In
`construct_reference_transcript_paths_gbwt_callback()` exon coordinates already
identify first/last node offsets while traversing an extracted source thread.
`augment_graph(..., is_introns=false, ...)` scans every edited step but passes
only partial-node mappings to breakpoint discovery. Full internal steps are
needed later for completed walks, not to identify cuts (see
`src/transcriptome.cpp` around 1560-1647 and 2505-2546).

The new representation retains source/exon ranges and boundary information,
then reconstructs edited walks on demand while preserving every named
transcript. The required gates compare node, orientation, offset, length and
order, then measure construction time and resident storage. First-encounter
order must remain identical for deterministic one-thread splitting.

The header-only `src/shared_transcript_path.hpp` implements immutable
shared source walks, clipped slices, reverse traversal and boundary-only
iteration. Its standalone bounded probe is
`shared_path_probe.cpp`; source validation happens once per source so repeated
transcripts do not repeat those checks. The integration below passed the unit
suite and bounded one-thread real-fixture byte gate. At that stage, full RNA
memory and runtime acceptance remained pending; the later V3 acceptance closes
them.

`EditedTranscriptPath` now supports shared slices on the GBWT reference route
when collapse is `no` and no path workspace is used. The internal
`use_shared_reference_paths` switch allows the expanded control in unit tests.
The original scan still determines transcript completion order and exclusion
at fragment breaks. A merged exon-coordinate union limits each immutable source
to overlapping nodes; it changes storage selection, not transcript selection.
Boundary discovery visits slice endpoints in walk order, while completed-path
conversion and splice-edge insertion decode the same mappings. Appending a
step takes the source pointer by reference, avoiding ownership-count changes
for each coalesced internal step.

Integration artifacts are under `shared_rna_integration_v1/` in the task root:
before-source snapshots, isolated patches, and the guarded build receipts.
The build used 48 GiB, no swap, CPUs 32-47 and the local wrapper, and left
the running prune dependency unchanged. The binary SHA256 is
`19cd4193f7642220b4100704fcfd13ce52c9f7c4c27f0f2dc01a420e49fed2b4`.
New tests compare shared/expanded
GBWT routes, including a real two-fragment gap, reverse transcripts, repeated
nodes, two exons in one node, completion ties and whole-node/no-augmentation
construction. All 774 assertions in the five `[transcriptome]` test cases
passed; the binary's test listing also confirms all three new cases are linked.

The real-fixture gate ran the retained V5 binary as a freshly measured control,
then the new shared candidate, with the same annotation, GBZ, flags, one thread
and CPUs 72-79. Both were in one sequential 16 GiB/no-swap service. Control
output matches the already accepted default-storage fixture; the new candidate
matches the fresh control byte for byte (graph and transcript info), passes
graph validation and matches the canonical fingerprint. No packed-history
exception or disk workspace was used.

| real small RNA fixture | expanded control | shared candidate |
|---|---:|---:|
| whole-command wall | 50.42 s | 55.13 s |
| user + system CPU | 48.25 s | 49.02 s |
| peak RSS | 627,720 KiB | 628,892 KiB |
| transcript parsing + graph update | 13.0455 s | 14.4602 s |
| graph/transcript output | 1.94893 s | 5.97743 s |

The single comparison shows 9.34% more wall time and 1.60% more CPU time.
Output serialization accounts for much of the wall difference even though its
implementation did not change; this does not establish why that interval grew.
The small fixture's peak remains dominated by loading the input graph. These
results establish bounded correctness, not a full RNA memory improvement or
a demonstrated full-workload speedup. Receipts are `build/acceptance.json`,
`checks/acceptance.json`, per-command time/status/phases, and
`fixture-review.json` under `shared_rna_integration_v1/`.

The standalone probe passed its explicit edge cases and 256 fixed-seed
randomized walk/boundary comparisons. It checks source ownership, clipped
single-node exons, fragment changes, repeated nodes, both orientations, moves,
and rejection of invalid sources/slices. A separate expanded oracle checks
the order and values of every mapping and every partial boundary. Both timing
modes preserve 10,000 paths and 5,120,000 mappings, with half the paths reversed
and the same checksum after two full traversal passes.

| isolated representation probe | expanded mappings | shared slices |
|---|---:|---:|
| representation capacity, including outer objects | 82,160,000 B | 26,000,000 B |
| peak RSS | 81,920 KiB | 26,624 KiB |
| construction | 0.169917 s | 0.026102 s |
| two traversal passes | 0.030667 s | 0.035589 s |
| whole probe wall | 0.21 s | 0.07 s |

Each mode also holds an 8,192-byte source. Both exited zero, used no swap and
reported no file-system input. These are single, synthetic representation
measurements: construction improved here while traversal was slightly slower;
they are not whole-command RNA timing or memory results. The two processes
ran sequentially on CPUs 72-73 in a 2 GiB/no-swap service after a shared-resource
preflight. The local build wrapper compiled only the standalone probe; the live
prune library and prototype sources were unchanged during the run. Receipts:
`tmp/transcript_memory_20260915/shared_rna_v1/acceptance.json`, the per-mode
`*.time.txt` and `*.status.json`, and `preflight.json`/`admission.json`.

At the end of the completed-sharing V1 stage, the full RNA run and its
automatic validator were stopped. Output construction was then unresolved:
reducing edited paths alone had not removed the later
completed-vector/embedded-graph peak. The V3 streamed-writer stage below
supersedes that implementation-state statement; the stopped mapped/spooled
candidate and its cleanup status remain historical facts.

### Streamed PackedGraph output and XG block decoding

V1 supplies the closed dense writer wire/resource evidence only: 50,003,968
deterministic repeated/reverse/nonmonotonic steps produced byte-identical
355,243,073-byte ordinary and generated outputs (SHA256 `e390892b...c853f`).
Under per-mode 8 GiB/no-swap bounds, generated output took 21.09 s and
12,288 KiB RSS versus ordinary output's 24.95 s and 557,056 KiB. The two mode
commands and hashes passed; the enclosing service returned 1 only because its
final manifest comparison formatted before/after paths differently. Do not call
that enclosing service a pass or imply that this V1 dense result was rerun on
V3. Receipts: `rna_stream_output_v1/dense-probe/acceptance.json`.

V2 failed only while compiling a unit fixture with a local variable-name
collision. Its failure receipt is retained. V3 fixes that fixture and retains
all-deleted source path slots, names and base path-ID offsets while requiring
fresh empty global memberships. It covers nonempty repeat/reverse source paths
destroyed before streaming, deleted-name reuse, zero added walks, and RNA
remove/sort/stream after removing an embedded source path.

The V3 guarded build is closed: binary SHA256
`ee63d53fa8ecfe67a2ca7a2f9281a90fc03ae2252950c5554fa2e8b61503d38c`,
with recorded BDSG/XG/source and unchanged-live-library identity. The focused
`[transcriptome],[packed][path][stream],[xg]` suite passed 22,849 assertions in
42 cases. Its real one-thread RNA fixture executed the streaming marker,
produced strict byte-identical graph and transcript info, equal canonical
fingerprint, and passed `vg validate`. V3 measured 49.10 s and 639,324 KiB
versus the retained V1 control's 49.82 s and 625,112 KiB. This small fixture is
dominated by GBZ/graph loading and does not establish full-workload memory or
runtime behavior. Receipts: `rna_stream_output_v3/build/acceptance.json` and
`rna_stream_output_v3/checks/acceptance.json`.

**Terminal full-command result and semantic status (2026-09-15):** V3 RNA
succeeded in `rna_stream_output_v3/chr21/` for service
`vg-memory-chr21-rna-stream-v3-20260915.service`, fixed invocation
`81be085f99544ce3b667dafd94f9b117`. On 24 threads/CPUs 200-223 under a
28 GiB/no-swap cap it took 0:54:11.56 wall (2,884.52 s user, 196.38 s system),
peaked at 18.5631 GiB (1.3079x OR), wrote the graph in 40:34.6 and `info.tsv` in
144.9 s. Full graph/path semantics are now accepted by the separate checker below.

The artifact-only strict gate completed stably at
`rna_stream_output_v3/chr21-byte-identity/acceptance.json` with exit 2:
`info.tsv` is exactly identical, but the candidate graph is 38,167,072,808 bytes
(SHA256 `357d12590c2c71993597f0d871ae84ceffe6e7d467cc599c476e8f5979f4fd53`)
versus control 38,148,975,648 bytes
(`d7772607ccd2a57940aabc01dfda32825cfa560b453e941492a645c45b22214d`), a
18,097,160-byte delta. Its source/binary/live-library and watched evidence
remained stable, so this is a completed mismatch, not a guard failure. This raw
byte gate did not establish semantics; the independent full check did.

**Full semantic gate PASS:**
`rna_stream_output_v3/chr21-semantics/acceptance.json` records `passed=true`,
`vg validate` exit 0, exact equality for all six canonical fields (node, edge
and named-path counts and digests), exact transcript-info bytes, and stable
input/output/tool identities. The service is terminal, MainPID 0/SubState
exited, invocation `8c1c3e00c81b4078907302b31d5b087d`. It ran under 64 GiB/no
swap on CPUs 88-95, with the retained `chr21_rna_fingerprint_v2/run` baseline.
Fingerprint wall time was 11:50.42, peak RSS 44,132,752 KiB, swap 0. This closes
the V3 RNA memory target: **18.5631 GiB, 1.3079x OR, 54:11.56 wall**, without
changing annotation selection. It does not validate newer parallel code.

The source-backed serial-stage audit is
`rna_parallel_output_v1/serial-stage-audit.md`. It distinguishes V3 phase-log
timings from loop-level inference, identifies the existing GBWT and vector
rewrite parallelism, and preserves mutable graph/order dependencies as unproven
until a deterministic design exists.

The output-architecture audit is complete and bounded parallel packing is now
implemented in the source-ready build candidate. The V3 baseline timing
partitions are 67.6 s parse/sort,
10.2 s construction (parallel), 337 s augment/update/copy-ID sorting (mixed),
140.35 s serial removal scan, 94.99 s sort/shared translation (mixed), 144.93 s
serial info writing with `endl` per row, and 2,434.61 s serial graph output.
The user requires all independent RNA computation to use available `-t` threads;
serial work needs an inherent ordering dependency.

The implementation contract is worker count from `-t`, bounded path blocks with
per-node last-occurrence summaries and prefix seeds, independent packing of the
actual `PackedVector`/`PackedPath` history, and bounded ordered drain. Error
handling must cancel, wake, and join workers. Root archives the current serial
production sources under `rna_parallel_output_v1/baseline-source/`; Sol owns the
libbdsg writer and packed tests, while root owns Transcriptome wiring and
independent info/transcribed-node work. Root's code now also performs bounded
parallel missing-edge discovery, followed by insertion in original order.
Node collection uses a dense or sparse rank table and worker bitmaps within a
1 GiB scratch budget; splice discovery uses about 8 MiB of batches with a
128-candidate/path fallback; info rows use bounded buffers and ordered output.
These stages report worker counts, wall time and process CPU time. The writer
uses a 3 GiB extra-memory budget and reports its internal stages.

V1 of this parallel work **passed its guarded build and focused correctness
checks**.
`rna_parallel_output_v1/BUILD_READY.json` pins the
production and test sources. The build receipt is
`rna_parallel_output_v1/build/acceptance.json`: binary SHA256
`b0fa7af0cdb9864482bcd3eb764c4de7bc7a515d674c3c4cc38f396ab6192b38`, BDSG
SHA256 `682cca911fc946ee2caf9697c3b8f5b39320e09c1be7aa24c33a6f5f89ca59bd`,
with source and protected live library unchanged. The terminal check receipt is
`rna_parallel_output_v1/checks/acceptance.json`: 48,455 assertions in 49 cases
passed, and real RNA fixtures at 1/2/4/24 threads passed validation, canonical
semantic identity and exact transcript-info identity. T1 retained raw graph
bytes in that gate; the expected thread-order-dependent raw differences at
T2/T4/T24 did not change semantics. Later V3 diagnosis shows this T1 raw match
was an observation, not a reproducibility guarantee.

Prepared artifacts are
`rna_parallel_output_v1/{build.py,launch.py,check.py,parallel_probe.cpp,parallel_probe.mk}`.
The same-binary exact-byte 50,003,968-step writer ABBA at 1/24/24/1 threads
averaged 28.1969 seconds serial versus 20.0230 seconds at 24 threads. Parallel
user+system CPU divided by wall averaged only 1.326. That regular-file result
triggered scheduler investigation but did not isolate packing from writeback.
Its launch gate did not pass, so `launch_chr21.py` did not launch a full
parallel RNA run.

V1's useful-parallelism gate failed before any full run was launched. The
exact-byte dense ABBA averaged 28.1969 s serial against 20.0230 s at 24
threads, but the parallel runs averaged only 1.326 CPU-seconds per wall
second, so the requested 24 workers were not being used. That failure is why
V2 changed the scheduling rather than the algorithm.

V2 replaces local-path coarse blocks with bounded small path batches and reuses
a persistent fixed ring of ordered output slots. Its header-only standalone gate
passed 42,497 assertions in nine cases, and
`rna_parallel_output_v2/standalone-r2/acceptance.json` records exact identity for
all four 355,243,073-byte dense outputs. This uses current header templates with
the pinned V1 archive; it is not a new `vg` build.

The regular-file ABBA cannot decide scheduler performance. The first serial arm
used 44.5751 s wall/23.5592 s CPU; the T24 arms used 28.1667/32.2965 and
31.0595/32.0186 s. One T24 `local-paths` stage used 18.5709/15.1059 s, but the
live serial writer was sampled in uninterruptible `balance_dirty_pages`, the
host had `vm.dirty_bytes=100000000`, and active prune shared the SSD. The next
bounded gate used the same pinned probes with output discarded to `/dev/null`.
`rna_parallel_output_v2/discard/acceptance.json` is terminal PASS with stable
inputs. Its V2 T1 arms averaged 20.2969 s; V1 T24 averaged 7.2406 s; V2 T24
averaged 1.9822 s. V2 T24 used an average 28.5043 CPU seconds, or about 14.38
effective cores. The V1 local-path arms used 5.561/6.166 s wall and
9.964/10.787 s CPU; V2 used 0.719/0.620 s wall and 12.524/11.916 s CPU, about
18 useful cores. This establishes a packing-only local scheduler win.

It does not establish regular-file or full-RNA speed: the regular-file arms are
writeback-confounded, V2 remains current-header standalone code rather than a
new `vg` build, and no whole-chromosome run launched. The terminal
500,039,680-step discard scale (`rna_parallel_output_v2/scale/acceptance.json`)
resolved the scheduling question: local paths remained parallel at 6.294 s
wall/120.222 s CPU, while membership offsets used 29.274/35.926 and next links
31.666/44.002. V3 source therefore uses page-aligned membership ID/offset
microblocks and stable-bucket next-link waves under the existing 3 GiB plan.
It preserves the coarse exact-anchor pass, sharded per-node carried heads,
ordered actual-page packing, partial-page carry, and a bounded serial fallback
for a single path larger than a wave. `rna_parallel_output_v3/BUILD_READY.json`
pins all seven source hashes. `rna_parallel_output_v3/standalone/acceptance.json`
is terminal PASS: 42,512 assertions in 11 cases, and both 50,003,968-step files
are 355,243,073 bytes with SHA256 `e390892b...c853f`. The terminal post-header
receipt records a same-binary 500,039,680-step V2/V3/V3/V2 discard ABBA:
70.7286 s V2 mean versus 20.7334 s V3 mean, or 3.4113x for this packing probe.
Offsets used about 18 effective cores, next links about 9.2, and local paths
about 20. The 2,056,621-node 50M-step T1/T24 files were byte-identical; T24 RSS
was 1,874,968 KiB (1.7881 GiB). V3's 500M peak was 2,829,364 KiB (2.6983 GiB),
and reported plans stayed within 3 GiB. Source, input, and live-library guards
were stable. Receipt: `rna_parallel_output_v3/post-header/acceptance.json`.

The guarded V3 build is terminal PASS: binary SHA256
`545de451f54b8bd291e4196e925dec4780f898bcb2d4bf96973b75d36b805f53`,
with source and protected-library guards stable. The first integrated gate ran
48,480 assertions in 52 passing unit cases but retained failure because its T1
real-RNA graph bytes differed from the older gold. Exact info, `vg validate`,
and the canonical fingerprint matched. The bounded diagnosis reran the same
binary: the repeat differed from the first T1 output and matched the gold,
while sorted ID-preserving GFA S/L/P/H records were identical for all three.
Decoded logical topology arrays and the membership suffix also matched. This
localizes the difference to packed storage encoding/history; its precise cause
is not isolated. `fixture-diagnosis/acceptance.json` and
`checks-completion-decision.json` retain that evidence and preserve exact
same-input synthetic writer byte gates. Completion checks, invocation
`9ae1e06e14754990b8c499b7388b530e`, are terminal PASS with
source/library/input guards stable. All 1/2/4/24 fixtures passed semantics,
exact info, and validation; the four same-input 355,243,073-byte synthetic
outputs had SHA256 `e390892b...c853f`. The full parallel chr21 command is
terminal success for `vg-memory-chr21-rna-parallel-v3-20260915.service`,
invocation `769f682f1e2f4b0db7a33daad48d230e`: exit 0, 33:31.11 wall,
5,201.04 user/205.47 system seconds, 19,438,672 KiB = 18.538162 GiB =
1.30615977x OR peak RSS, and swap 0. The 38,126,287,944-byte graph was streamed;
transcript info is byte-identical and watched inputs are unchanged.
`rna_parallel_output_v3/chr21/command-acceptance.json` closes the command,
1.5x-memory, info, and input gates. At that historical boundary it explicitly
left graph semantics pending. Its SHA256 is
`592cda48a681d456e320721055b0e6aba086308c768f54b14d27edb54ac23059`.

The checksum-backed `rna_parallel_output_v3/chr21/phase-analysis.json` (SHA256
`ec312081003cbaeb16fe897c7276f7fdb0583a0f37529b803bc1d90cb08340d3`) binds GNU
time, status, stderr, phase log, RSS samples, and the 574-file source-hash
manifest; no source changed. Splice-edge discovery measured 40.3467 wall/
870.446 CPU seconds (21.57 effective cores), transcribed-node collection
5.03616/80.8338 (16.05), path metadata 87.7852/87.7716 (1.00), and the nearest
0.5-second sort/compact samples 153.534/153.520 (1.00). Sort/compact still
combines topology ordering, translation-map construction, graph mutation, and
path translation, so the last figure attributes no subregion. Output stages
were frequently sampled with the leader in `balance_dirty_pages` (for example,
759/920 next-link and 1,176/1,393 local-path samples); these are diagnostic
snapshots, not whole-process idle fractions or proof of storage saturation.
The command wall is 38.1494% below the accepted serial V3's 54:11.56, but the
control was not rerun and the host was nonexclusive, so this is not a controlled
causal speedup.

Independent validation is terminal success as
`vg-memory-chr21-rna-parallel-terminal-v3-20260915.service`, invocation
`a64d62b5b6f04d0690d99bf3108f706f`, under 64 GiB/no swap on CPUs 88-95.
`rna_parallel_output_v3/chr21-terminal/acceptance.json` passes through the
canonical semantic fallback: candidate and serial raw graph bytes differ, while
`vg validate` exits zero, all six canonical fields match (2,056,621 nodes,
2,726,485 edges, 5,607,688 named paths and all three hashes), transcript info is
byte-identical, and evidence guards remain stable. This closes parallel V3 at
**18.538162 GiB, 1.30615977x OR, 33:31.11 wall, swap 0**. The serial V3
**18.5631 GiB, 1.3079x OR, 54:11.56 wall** result remains the previous accepted
baseline. `chr21/phase-analysis.json` is immutable historical command evidence;
its pending wording is superseded by this terminal receipt. Neither result is a
production-index result. The residual metadata/translation audit remains
`rna_parallel_output_v3/serial-followup.md`.

The accepted parallel result is the workable baseline. The private attribution
pilot `vg-memory-rna-metadata-pilot-v1-20260916.service`, invocation
`9c6d2ba45e3c40cb8c4b6aaac7ae94ad`, is terminal PASS with source and input
guards stable. Its 5,607,688-name control metadata stages measured 92.1878 and
85.025 s. Instrumented metadata measured 82.5315 and 85.6669 s; deterministic
jittered samples estimate `has_path` at 16.2978 and 16.4012 s and
`create_path_handle` at 34.1397 and 34.7464 s. All arms peaked at no more than
5,553,780 KiB. Clock-corrected name/head-tail estimates were negative and the
control/instrumented variation prevents a speedup claim. Receipt:
`rna_metadata_parallel_v1/pilot/acceptance.json`.

Source review confirmed `has_path(name)` and `create_path_handle(name, false)`
duplicate name encoding and lookup. The private candidate removes exactly the
outer check in both serial and parallel writers. `create_path_handle` remains
the duplicate authority; its public duplicate failure is `runtime_error` rather
than the writer's former `invalid_argument`, and the writer API promises neither
class nor message.

That lookup gate is terminal PASS as
`vg-memory-rna-metadata-lookup-v1-20260916.service`, invocation
`52f675bae8284b1fb8051468f5216ff8`.
`rna_metadata_lookup_v1/checks/units/stdout` reports 42,622 assertions in 14
cases; `checks/acceptance.json` binds exact retained 100,000-name graph bytes at
T1 and T24, stable guards and all 574 production sources unchanged. For the
5,607,688-name one-step CPU-only ABBA, mean whole-writer wall fell from 93.4301
to 77.5504 s (17.0%), metadata from 87.3692 to 71.2887 s, and mean writer CPU
from 128.2705 to 113.113 s. Candidate/control peak RSS was 5,549,308/5,553,776
KiB. Correctness and the declared useful-performance gate both passed.

This accepts the private prototype, not an integrated `vg` result or full-RNA
speedup. The apply-ready `rna_metadata_lookup_v1/candidate.patch` has SHA256
`65fff58d6f6c5f1edc2e26aabc0971823e61f3013ad6d5498dfc6d8932bf2b37`;
`patch-preflight.json` records `git apply --check` exit zero and `applied=false`.
The current-binary prune command and proof-backed terminal checker are now
accepted. The next authorized implementation step is to integrate exactly the
two removals and private unit diff using `rna_metadata_lookup_v1`'s integration
scripts, then run its guarded build and appropriate fixtures. A whole-RNA rerun
is unnecessary absent a new integration, correctness, performance, or provenance
concern. Shared-source translation and compaction subregion timing remain
optional later work.

`rna_parallel_output_v2/README.md` is the compact V2 receipt index. Its matched
16/64/64/16 GiB file-output control found all writers in `balance_dirty_pages`;
cap means differed by about 6%, less than within-cap temporal variation. It
establishes no MemoryMax benefit, so resource policy remains unchanged. No
production-index or annotation/paralog claim follows.

The historical V5 checker launcher corrected the active-RNA reserve from 20 to
28 GiB (required 252.25 GiB), without modifying the run:
`chr21_prune/v5-terminal-check-resource-update.json`, SHA256
`3b58d67a7303c5909ab3f3ee7e1e07547cc96be04a6d2d552802bf3280d590c3`. V5
later timed out and was rejected without that semantic validation, as recorded
below.

### XG block-decoder dense A/B result

The block decoder's full XG-construction cost was measured on the retained
synthetic 50,003,968-step PackedGraph from the V1 dense writer gate: 12,208
paths of 4,096 steps over 16 repeated/reverse/nonmonotonic nodes. It is a
construction microbenchmark, not biological validation.

The 8 GiB/no-swap ABBA service completed under CPUs 112-113 with 300 seconds per
arm: predecoder `vg-xg-move` (`b87e0672...a11299`), V3 `vg-stream-rna`
(`ee63d53f...3d38c`), V3, predecoder. Every output is 552,532,629 bytes with
SHA256 `67a23152...d34ff5`; input, both binaries and live `libhandlegraph.so`
were unchanged before/after. GNU-time results were old 1:32.96 (64.52+2.52 CPU
s, 1,090,404 KiB RSS) and 1:39.13 (65.84+3.90, 1,070,300 KiB); new 0:45.99
(17.21+3.60, 1,070,684 KiB) and 0:37.57 (13.97+3.34, 1,067,116 KiB). All arms
exited 0, used zero swap and reported zero filesystem input; output accounting
was 1,098,168--1,122,680 blocks. The service peak was 2,804,391,936 bytes.

ABBA mitigates first-read order but does not make this a production or
whole-prune benchmark: the input was already cached (zero reported input), has
synthetic path structure, and measures `index -t 1` only. It establishes the
block decoder is materially faster for this bounded XG construction while
preserving exact bytes; it does not attribute a whole-prune speedup. Receipts,
per-arm phase/RSS/time records and the driver are
`xg_block_decode_v1/dense-ab-v1/acceptance.json` and siblings.

Sol's read-only target assessment finds no clear path to the ordinary-prune
<=1.5x RSS target. The V2 late extend phase has a distinct sampled 74.692 GiB
peak beyond XG's ownership; block decoding changes runtime only, and a possible
move-assignment allocator-page release cannot establish the required 11.813 GiB
reduction. Do not schedule a speculative full ordinary-prune run on this basis.

The real-fixture records remain separate: old mapped `xg_move_assignment_v1`
took 16:08.49 with 313.60 CPU seconds and 7,848,176 KiB at 24 CPUs; new mapped
took 9:13.06 with 283.71 CPU seconds and 7,865,868 KiB while capped to eight
CPUs. The semantic ordinary control took 5:13.88 with 154.49 CPU seconds and
6,797,380 KiB under unknown affinity. They are correctness/regression records,
not a controlled prune speed claim.

The separate prune XG fixture check completed successfully through
`rna_stream_output_v3/launch.py prune-checks`: service
`vg-memory-stream-rna-prune-checks-v3-20260915.service`, invocation
`ad305d4bf86b4196825549c2a89419d6`, 16 GiB/no swap and CPUs 64-71. `t/38_vg_prune.t`
passed all 26 TAP checks; the dense XG output is byte-identical to its retained
control. Default and mapped tiny prune fixtures passed strict graph/mapping byte
identity, path verification and validation at 1, 4 and requested 24 threads.
The real mapped fixture passed the same gates (2 GiB retained workspace): 9:13.06
wall and 7,865,868 KiB RSS, versus the retained `xg_move_assignment_v1`-era
pinned control's 5:13.88 and 6,797,380 KiB. This is a different binary and run
cache/host state, and the new run capped `-t 24` at the service's eight CPUs; it
is regression evidence, not a prune speed or memory improvement claim. Receipts:
`rna_stream_output_v3/prune-fixtures/acceptance.json` and per-gate artifacts.
Prune V5 is terminal and rejected. It reached its exact eight-hour service
deadline with systemd `Result=timeout`, `ExecMainStatus=15`, code 2, and a
62 GiB peak. There is no measured status, GNU-time receipt, output graph, or
semantic check. Authority: `chr21_prune/candidate-v5-terminal-timeout.json`.
Ordinary V2 remains the accepted prune result at 75.608 GiB and 4:14:01.

Serial and parallel V3 full RNA are closed by their terminal semantic
acceptances above. V1
parallel correctness gates are also closed, but its useful-CPU gate failed. V2
standalone, discard, and 500M scale receipts are terminal. Parallel V3
header-level standalone, post-header, guarded build, focused checks, and the
full-chr21 command/resource gate pass. Its initial integrated gate exposed
semantically neutral T1 raw-packing variation; the bounded diagnosis and
completion checks then passed. Independent full-chr21 graph validation is
terminal PASS through the canonical semantic fallback. No result may be
promoted to a whole-genome production index or an annotation/paralog conclusion.
XG block decoding remains a bounded microbenchmark, not a whole-prune speed claim.

This completed-sharing result alone did not establish the final memory target:
at that stage, mutable PackedGraph embedding remained a separate requirement and
a bulk/frozen writer was only a future design. The subsequent V3 streamed writer
has now passed terminal full-chromosome memory and semantic acceptance as
recorded above. That acceptance does not cover V2 end-to-end performance; only
its standalone packing behavior is measured.

### Completed shared RNA paths: implemented and bounded-validated

The follow-up now preserves shared source slices in `CompletedTranscriptPath`
instead of expanding every edited path into a separate handle vector. Its
`TranslationCache` registers all slices in a batch, translates each source once,
aligns clipped endpoints to the resulting whole nodes, and retains reverse walk
order. It omits source intervals outside all registered slices; these can name
nodes removed before compaction, so translating them would be incorrect. Weak
ownership checks protect cache keys, and the cache releases each translated
source's bookkeeping after its final registered use without retaining the old
source. Node chopping, subsequent augmentation, and ID compaction use this route.

Completed consumers now iterate either resident representation: splice edges,
transcribed-node collection, embedding, transcript-body anchors, FASTA, GBWT and
info output. Explicit reference/haplotype-copy getters materialize their copies
for the existing vector API. The internal `transcript_paths()` view retains
shared storage and must be traversed through `for_each_handle()`. Sorting builds
translations for retained graph nodes instead of scanning all shared path
occurrences merely to discover used handles. In this completed-sharing stage,
mutable PackedGraph embedding was still the existing implementation; V3 later
adds the streamed writer described above. No full RNA memory target is claimed.

Artifacts are in `shared_rna_completed_v1/` under the task root. The guarded
48 GiB/no-swap build completed with source hashes and the live prune library
unchanged, producing binary SHA256
`cb6ad7d0d6a971eb41bf47a426e89926f776ae382c4a0a8bf0d7afb66d535beb`.
The subsequent 16 GiB/no-swap service completed all 906 assertions in eight
`[transcriptome]` cases and the matched real fixture. Tests include reverse and
repeated nodes, multiple cuts, actual fragment-gap exclusion, a second reference
augmentation, FASTA/GBWT output, and a backing source demonstrably retaining a
node unused by its surviving transcript before deletion/compaction.

The real fixture's graph and transcript info match the fresh expanded control
byte for byte, with graph validation and canonical fingerprint equality. The
control also matches the retained accepted fixture. No disk workspace or
packed-history exception was used.

| small real RNA fixture | expanded control | shared edited + completed |
|---|---:|---:|
| whole-command wall | 56.57 s | 53.34 s |
| user + system CPU | 51.60 s | 50.08 s |
| peak RSS | 633,996 KiB | 630,232 KiB |
| transcript parsing + graph update | 13.5205 s | 14.9764 s |
| filesystem input (GNU time) | 295,008 | 0 |

Cache conditions differ, and the annotation is small (432 constructed paths).
The result establishes bounded correctness and no large whole-command runtime
regression in this comparison; it does not establish a causal speedup or
full-workload memory savings. Receipts: `build/acceptance.json`,
`checks/acceptance.json`, per-command measurements and `fixture-review.json`.

The separate `shared_translation_probe.cpp` validates exact source partitions,
clipping, skipped stale source entries, ownership/counter invariants, failed
translation retry, and old-source release. Its synthetic 10,000-path test
produces 10,240,000 equal mappings: expanded mapping-record storage is
164,080,000 bytes versus 816,384 bytes for shared storage, and only 512 source
mappings are translated. Construction is 0.371496 versus 0.004261 seconds;
traversal is 0.030766 versus 0.181842 seconds. Total process wall is 0.42 versus
0.18 seconds, peak RSS 159,744 versus 2,048 KiB. These compare 16-byte mapping
records, not the old 8-byte completed-handle payload, and shared traversal is
slower in this probe. Exact commands, source/binary hashes and measurements are
under `translation-probe/`; the header hash is
`dabf50d247da4c49f6bcf07d130ef326cdf462aaea19278b8141dfa290625a2a`.

At the end of this completed-sharing stage, the next implementation requirement
was graph output without retaining every mutable path membership/link record.
That requirement is superseded by the V3 streamed writer and its live full RNA
validation recorded above. The rejected mapped RNA run and automatic validator
remain stopped; cleanup and all prune/GCSA workspace retention remain unchanged.

## Current full validation (2026-09-15)

- **RNA V4 stopped at the user's request:** invocation
  `79bdbf7d31b249c89d355d9857ad6c01`, former vg PID 1946186, pinned binary
  `mapped_input/build-integrated-v3/vg-mapped-paths`. Its 20 GiB/no-swap run
  did not complete. Scratch payloads are removed; diagnostic receipts remain.
  The later V3 streamed writer closed full RNA memory and semantic acceptance;
  the newer parallel route remains gated separately.
- **Prune V5 timed out and is rejected:** invocation
  `54b7558c8cbe40b2905df354cdf8c6f8` ended at the exact eight-hour deadline
  (2026-09-15 23:16:53 UTC) with `Result=timeout`, `ExecMainStatus=15`, code 2,
  and a 62 GiB peak. The wrapper produced no measured status or GNU-time receipt;
  stdout is zero bytes and no graph or semantic validation exists. The workspace
  is preserved: `graph.arena` is 128 GiB apparent/43.62 GiB allocated and the
  mapping is 16 bytes. No restart or cleanup is authorized. Receipt:
  `chr21_prune/candidate-v5-terminal-timeout.json`.
- **Prune V4 failed by OOM and is preserved.** The completed default V2 prune
  result still satisfies the required <2x target. V5 supplies no accepted
  memory, performance, or equivalence result.

**Timestamped live observation, 2026-09-15T14:42:51-05:00:** the monitor found
PID 3413100 running on CPUs 136-159 at 265.96 minutes, with 57.373 GiB VmHWM
and last message `Removed small subgraphs`. This is an observation, not a
permanent status fact or terminal acceptance; rerun the monitor before acting.

**Phase-transition observation, 2026-09-15T16:51:52-05:00:** the same service,
invocation and PID are still running. The retained phase log now reaches
`Unfolded graph: 6077286 nodes, 6185993 edges on 216871 paths` at Unix time
1789506931.9819062, about 6.1 hours after the original-graph message. This is a
live post-unfold observation only; command completion, peak acceptance and
graph/mapping equivalence remain absent.

**Latest live observation, 2026-09-15 22:12 UTC:** prune V5 remained running at
6h56 elapsed. It had already run materially longer than the accepted default V2
command's 4:14:01. This remains a live timing observation, not terminal memory,
output or equivalence acceptance.

**Terminal observation, 2026-09-15 23:16:53 UTC:** the same invocation reached
its exact eight-hour deadline and systemd terminated it. `MemoryPeak` equals the
62 GiB cap, but failure before any measured status, GNU-time receipt, graph
output, or semantic check means that peak is not an accepted memory result. The
partial arena and mapping are retained solely as failed-candidate evidence.

The former monitor at `tmp/transcript_memory_20260915/mapped_input/full-v5/monitor.py`
distinguishes the two pinned binaries, but V5 is now terminal. The plan,
successful preflight, observer handoff and launch receipts are in that same
directory. The RNA V5 watcher originally observed the existing
RNA V4 run and reserved capacity for prune V5. It is now intentionally disabled
under the runtime requirement above; its inactive state is expected.

The RNA time extension used a runtime service override and configuration
reload, after a 16 MiB dummy-service probe survived its original deadline and
finished normally with the same PID. This host rejected `set-property` for
`RuntimeMaxUSec`; that failed probe is retained separately. The successful
probe confirmed unchanged indexing-job identities and limits. Actual extension
admission found 415.24 GiB shared headroom against a 274.25 GiB reservation and
10.007 TiB SSD free. The RNA process, command and executable stayed identical;
its memory/swap limits and the prune/watcher units also stayed identical.
Receipts: `mapped_input/full-v5/runtime-extension-probe-v1/`,
`runtime-extension-probe-v2/` and `rna-runtime-extension.json` in that directory.

Prune V5 reached its original-graph message after 1,765.52 seconds (29m26s),
reporting 2,056,621 nodes and 2,726,485 edges. The input-load interval's sampled
maximum RSS was 775,798,784 bytes (0.723 GiB); this is a phase observation,
not a terminal whole-command peak or an equivalence result. XG construction
and unfolding remain ahead. The fixed boundary and sampled summary are
`mapped_input/full-v5/prune-input-load-boundary.json` and
`prune-input-load-summary.json` in that directory.

Prune V5 emitted `Removed all paths` at 143.285 minutes from command start,
113.860 minutes after its original-graph message. A subsequent live observation
showed about 25.9 GiB RSS, with reverse-index construction still running.
The fixed phase event and adjacent RSS samples are retained in
`mapped_input/full-v5/prune-paths-removed-boundary.json`. This is not terminal
memory or output acceptance.

## Acceptance targets

GNU `time` receipts in
`/mnt/ssd/lalli/.claude/jobs/0c05a913/tmp/chr21_downstream/or/` give:

| command | OR peak KiB | exact-only target GiB | stretch GiB |
|---|---:|---:|---:|
| RNA | 14,882,308 | <28.3857498169 | <=21.2893123627 |
| prune | 43,955,792 | <83.8390197754 | <=62.8792648315 |

The original exact-only peaks are 103,942,808 and 269,165,772 KiB,
respectively. These are RSS targets, not serialized-size targets. The new
comparisons use matched inputs and thread counts. Parallel V3 RNA meets both its
<2x and 1.5x stretch targets at 18.538162 GiB with the semantic gate above;
serial V3 at 18.5631 GiB remains the previous accepted baseline. Full chr21
prune V2 meets the <2x target at 75.608 GiB with the pair-specific storage
exception documented below. The current-binary ordinary revalidation also meets
the <2x target at 74.815071 GiB (1.784731532x OR), with its own closed
pair-specific proof. Neither V2 nor the current-binary run establishes the
62.879 GiB 1.5x target; that target remains unmet.

## Separate efforts

- **Indexing implementation:** this work changes storage and lifetimes in vg,
  XG, and libbdsg. Existing external-memory GCSA2 workspaces remain untouched.
- **v2 production indexing:** governed by the consuming workspace's
  `WORKSPACE_STATE.md`, superseding Section 21. No production graph bank, global
  mapping, prune output, or GCSA workspace is changed by these experiments.
- **Annotation correction and paralogs:** a separate scientific effort. This
  implementation does not change selection, deduplication, or correction rules.

## XG reverse occurrences

`deps/xg/src/xg.cpp::index_node_to_path` now counts visits per node, allocates
the final arrays at their measured integer widths, and fills them directly.
Walking paths in rank order and emitting each path's forward visits before
reverse visits reproduces the old external sort's tuple order. Temporary
storage is one 64-bit cursor per node; no step-sized occurrence file is built.
This replaces the estimated 126.37 GiB exact-only temporary occurrence file and
avoids initially allocating three arrays at 64 bits per entry. The full V2
result below measures the combined reverse-index and early-release changes;
it does not apportion the RSS saving between them.

An optional input-consumed callback now releases the mutable graph's paths
after XG has copied them, before its final reverse-index allocation. When XG
validation is requested, the callback runs after validation's final input read.
The new lifetime tests destroy the input graph inside the callback and compare
the completed XG bytes. The rebuilt candidate passed 1,465 assertions across
30 XG/spool test cases, plus all 26 pruning TAP checks; receipts are in
`xg_consume/`.

The first version, with direct occurrences but without early path release,
passed 1,426 assertions in 26 XG test cases and all 26 pruning TAP checks. A
two-million-step storage fixture produced byte-identical XG output at 66,408
KiB peak RSS versus the pinned control's 180,076 KiB. Wall times were 3.10 and
19.86 seconds, respectively. These are fixture measurements, not a pangenome
speedup claim; the storage fixture includes walks used to exercise occurrence
ordering, while separate valid graph fixtures check pruning semantics.

## RNA storage

A completed-handle drain alone cannot meet the target: the earlier phase peaks
at 78.57 GiB, and the ordinary output graph is 35.53 GiB on disk. The planned
opt-in route uses edited/completed walk spools and a file-associated mapped
graph with explicit page eviction. It must serialize ordinary PackedGraph
bytes without constructing an in-memory PackedGraph copy.

The first storage layer passed the complete libbdsg test suite under a 24 GiB
cap (868,176 KiB peak, exit 0). It creates a file-associated mapped graph,
explicitly checkpoints/evicts pages, and writes ordinary PackedGraph bytes
without making an in-memory graph copy. Its native mapped format remains
separate from ordinary PackedGraph format. The opt-in RNA integration now
implements edited/completed step spools, chunked boundary/node collection,
ID remapping and embedding, cached transcript lengths, and mapped graph copying
and output. Its targeted object build passed. The first complete build exposed
an include-order ambiguity between handlegraph and POSIX `off_t`; the spool now
explicitly uses `::off_t`, and the complete build passed. The pinned RNA
candidate passed 2,213 assertions across 32 XG, spool, and Transcriptome cases.
All four early CLI rejection checks also passed. The first real one-thread
fixture, using the default storage route, matches the control's canonical
fingerprint and transcript-info bytes and passes graph validation, but its
PackedGraph serialization is 26,880 bytes smaller. Converting both with the
same earlier binary gives byte-identical complete GFA output, including node
IDs, sequences, edges, and all named walks. A bounded diagnostic using the
pre-RNA-change V2 binary reproduces the new default candidate's bytes exactly
(SHA256 `b319dc14689ccb8fb55a48f225d53e7a6925ac056cf5c9c9e9a42030ac61f911`).
Thus the mismatch predates the RNA/libbdsg integration. Decoding `graph_iv`
finds identical logical values with different historical compression anchors;
the entire suffix after that field is byte-identical. The exact older-build
origin remains unknown. Workspace one-thread validation now uses this matched
pre-RNA build as its byte oracle and retains the original pinned graph as an
independent semantic oracle. No byte gate was converted to a content-only gate.
The first workspace one-thread fixture also matches the semantic fingerprint,
info bytes, and graph validation, but differs by another 960 serialized bytes.
The bounded wire decoder localizes these to historical anchors/widths in
`graph_iv` and `edge_lists_iv`: every decoded value matches and all remaining
bytes match. PagedVector anchors are chosen by the first nonzero assignment,
and widths retain historical maximum encoded differences. Those bytes therefore
describe mutation history in addition to the final values. The explicit
`packed_backend_history_exception` permits differences only in those two
vectors' physical encoding, requiring their decoded values and structural
counts to match and every byte outside them to match exactly. Default storage
and identical-operation unit fixtures retain raw byte gates. Canonical graph
fingerprints, info bytes and graph validation remain mandatory. The comparator
rejects logical-value, suffix-byte, unused-slot, capacity and truncation
negative fixtures. Subsequent reserved-arena fixtures and full-run state are
recorded below.
The workspace command itself exited 0 at 527,364 KiB peak RSS and 12:56.18 wall.
Its conversion/deletion/embedding stages took 277.10/476.98/0.83 seconds,
respectively, versus 17.35/16.83/0.13 seconds for the matched pre-RNA control.
These small-fixture costs do not establish full-workload memory or runtime.
Receipts are in `rna_spool/default-t1/` and `rna_spool/pre-rna-control-t1/`.
The initial route is scoped to the
supplied `-z -j -c no -r -d -i` recipe; optional output consumers and intron mode
retain their existing implementation until separately supported.

`--path-workspace DIR` requires a new directory and rejects unsupported option
combinations before opening requested output files. Its retained arena and
spools are scratch evidence, with no resume support. The existing durable GCSA2
index workspaces are independent and remain untouched.

## Receipts and bounded checks

Task-owned artifacts are under `tmp/transcript_memory_20260915/`:

- `xg_direct/`: builds, two-million-step XG comparison, and pruning fixture.
- `xg_consume/`: early input-path release build and validation receipts.
- `rna_fixture/`: complete linked exon/body/retention models and a pinned-binary
  RNA control. There are 108 body groups, 432 Parents, and 8,424,825 feature bp.
  The control embedded 432 paths and exited 0: 628,080 KiB peak RSS, zero swap.
- `chr21_prune/control-input-output.sha256`: fresh hashes of the retained exact
  genic input, guide, completed pruned graph, and completed mapping. The finished
  mapping is an expected output; new runs must start from a fresh two-u64 seed.

The pruning fixture captures the retention-pad walk in the guide before
stripping its graph path. The pinned control removed all original topology and
unfolded three protected walks into 13 nodes / 14 edges. Verification passed at
1, 4, and 24 threads. The first patched version matched graph **and mapping**
bytes at all three thread counts, with graph validation passing. The
early-release version also passed all three thread counts. A second fixture
derives from the real RNA control, with retention-pad protection captured before
removing pad paths. Its pinned control completed at 6,797,380 KiB peak RSS and
5:13.88 wall; the early-release candidate completed at 6,810,352 KiB and 5:12.02,
with graph/mapping byte identity, path verification, and graph validation all
passing. This real fixture showed no whole-command RSS saving. Its earlier
sampled peak was lower, but the final serialization raised the high-water mark.

Full exact-only chr21 prune ran as
`vg-memory-chr21-prune-v2-20260915.service`, under a 96 GiB cap, no swap, and a
six-hour timeout. It reuses the retained exact genic graph/guide, 24 threads,
and CPU affinity 232-255. Fresh admission found 405.67 GiB shared headroom and
10.3789 TiB SSD free. `chr21_prune/candidate-v2-admission.json` records the gate;
`chr21_prune/candidate-v2/` retains inputs and terminal measurements. The command
finished at 79,280,776 KiB (75.60804 GiB), 1.80365 times OR, with exit 0.
Mapping bytes match and graph validation passes, but the graph byte gate fails:
candidate SHA256 `7844027c47892d846fc46c2995f23fd88baff864984769ac9b7d0a13b7562572`
differs from the retained control. The harness therefore exits 1. The entire
201,984-byte size difference lies in `edge_lists_iv` packed-page payloads.
Subsequent exact node-ID/sequence/oriented-adjacency comparison passed for all
7,621,792 nodes and 16,484,326 outgoing entries. Streaming wire comparison
established identical values in all 33,556,480 edge-page slots, despite 2,630
different page widths. The only other differing bytes are 1,412 historical
anchors in `path_membership_node_iv`; all its encoded page payloads and logical
heads are zero. All bytes outside these two members match. This supports the
pair-specific packed-history exception in `candidate-v2/semantic-acceptance.json`:
the <2x memory and unchanged-graph/mapping contract passes, while the original
raw byte-gate failure is retained. The <=1.5x stretch target does not pass.
Diagnostics are under `chr21_prune/pathless_compare_v1/` and
`chr21_prune/candidate-v2-byte-diagnosis/`. The comparator passed negative tests
for changed sequences, edge orientation, node IDs and nonempty paths, then
compared the full pair in 29.86 seconds at 11,044,864 KiB. No production index
workspace is used.

The next candidate adds an opt-in standard PackedGraph reader into a mapped
arena, `--path-workspace` and `--path-workspace-reserve N` to prune, periodic
eviction during XG's input-path traversals, and release of XG/GBWT before final
output. A new XG callback reports each path during both enumeration passes;
it cannot mutate the input. The separate input-consumed callback remains the
only destructive handoff. These changes are not in the completed V2 binary.
The inverse reader and sparse initial arena passed the complete libbdsg suite
in `mapped_graph_foundation/attempt_reader_v5/`: exit 0, 2:16.76 wall,
1,244,908 KiB peak RSS under a 24 GiB cap. Failed compile attempts remain
retained. The combined vg build completed in `mapped_input/build-integrated-v1/`
with unchanged source hashes. Its pinned binary passed 2,225 assertions in
33 unit cases, all 26 pruning TAP checks, seven RNA and eight prune CLI checks,
and mapped retention-protected pruning fixtures at 1, 4 and 24 threads. Graph
and node-mapping bytes matched at every thread count. Receipts are in
`mapped_input/acceptance-v1/`. The larger prune fixture also passed graph/mapping
byte identity, retention-path verification and graph validation: 7,856,324 KiB
peak RSS and 9:51.65 wall, versus the retained ordinary control's 6,797,380 KiB
and 5:13.88. This is a higher fixture RSS, not a memory win. Its outputs and
retained arena are in `mapped_input/prune-real-v1/`.

In `mapped_input/rna-v1/`, the default one-thread RNA candidate matches the
matched-build control's graph and info bytes, fingerprint and graph validity:
624,428 KiB, 51.73 seconds. The workspace one-thread run with a 2 GiB initial
arena passes the narrow backend-history exception, graph fingerprint, info
bytes and validation: 544,184 KiB and 9:19.88. Conversion/construction/deletion
took 209.384/16.5592/332.978 seconds; embedding took 0.435702 seconds. The
24-thread workspace run also passed fingerprint, transcript-info byte identity
and graph validation, at 545,440 KiB and 8:58.29. Raw wire differs under the
existing multithreaded ID/order contract; no backend-history exception is used
for that comparison. Actual fixture CPU bindings are in the command receipts.
See [the storage contract](MAPPED_GRAPH.md).

## Active full chr21 validation

The superseded V3 mapped full attempts used the pinned integrated binary
`mapped_input/build-integrated-v2/vg-mapped-paths`, SHA256
`d32567976b0e016e465ddaaded2c1c41d557c23089f1f8328a12b23515b4b70e`.
The final libbdsg cleanup preserves the original exception and unregisters the
allocator chain after failed native loading; its complete suite passed in
`mapped_graph_foundation/attempt_reader_v7/`. The final vg binary passed the
unit/TAP/CLI/tiny-prune checks again and the matched default RNA byte gate in
`mapped_input/acceptance-v2/`. `v1-v2-validation-bridge.json` records that only
help formatting and the exception cleanup/test changed after larger fixtures.

| run | service | cap | task-owned output | state |
|---|---|---:|---|---|
| prune, direct XG occurrences and early path release | `vg-memory-chr21-prune-v2-20260915` | 96 GiB | `chr21_prune/candidate-v2/` | <2x and equivalence pass with documented packed-history exception; raw byte failure retained |
| RNA, mapped graph and spooled paths, old allocator | `vg-memory-chr21-rna-mapped-v3-20260915` | 20 GiB | `rna_spool/chr21-candidate-v2/` | intentionally stopped after replacement fixture acceptance; partial evidence retained |
| prune, mapped input, old allocator | `vg-memory-chr21-prune-mapped-v3-20260915` | 62 GiB | `chr21_prune/candidate-v3/` | intentionally stopped after replacement fixture acceptance; partial evidence retained |
| RNA, allocator correction and reduced sorting scan | `vg-memory-chr21-rna-mapped-v4-20260915` | 20 GiB | `rna_spool/chr21-candidate-v4/` | stopped by user for runtime; scratch removed, diagnostics retained |
| prune, allocator correction | `vg-memory-chr21-prune-mapped-v4-20260915` | 62 GiB | `chr21_prune/candidate-v4/` | OOM during XG construction; failed workspace and receipts retained |
| prune, XG move-assignment correction | `vg-memory-chr21-prune-mapped-v5-20260915` | 62 GiB | `chr21_prune/candidate-v5/` | rejected: exact eight-hour timeout at cap; no measured status, output graph, or equivalence check; workspace retained |

All are candidate validations, not accepted production indexes.
The mapped runs use 24 threads, explicit CPU bindings 200-223 and 136-159,
respectively and zero swap. V3 and the initial V4 launches used six-hour limits;
RNA V4 was extended to eight hours before its intentional stop, and prune V5 launched with eight hours,
as recorded in Current full validation above. Each reserves one 128 GiB sparse
arena; logical reservation is not resident memory or allocated disk blocks.
Fresh joint admission found 353.26 GiB shared headroom and 10.369 TiB SSD free
against 210 GiB required (82 GiB caps, 64 GiB reserve, 64 GiB other-job growth).
Those initial V3 receipts remain in `mapped_input/full-chr21-v3-admission.json`
and launch manifests. The V4 replacement uses the fully fixture-accepted
`mapped_input/build-integrated-v3/vg-mapped-paths` binary, SHA256
`aed50ea90bb2aa8f3311a628b4df54ea2bfdc3241a54cc6e4a98a0c8fa3c570d`.
Its admission found 452.54 GiB shared headroom and 10.191 TiB SSD free against
274 GiB reserved (the two caps, 64 GiB reserve, 64 GiB other growth, and a
conservative additional 64 GiB for possible old validation). MemoryMax, zero
swap and actual process affinities were verified. Launch and retirement
receipts are in `mapped_input/full-v4/`; `monitor.py` appends current observations.
The old invocations were identified before stopping, their processes were
confirmed absent afterwards, and all scratch files were retained. They are
interrupted attempts, not completed benchmark results. The first quiescence
check ran before an old PID disappeared; the retained follow-up confirms
retirement. No old workspace is resumed by the fresh V4 commands.
No consuming production graph bank, index workspace or annotation is modified.
RNA graph validation/fingerprinting will run separately under a 64 GiB cap
after terminal command success; the completed baseline fingerprint is reused.

**V4 construction boundary, still nonterminal:** RNA logged construction of all
5,607,688 reference transcript paths at Unix time 1789477169.0218663. The
interval after transcript parsing took 3,914.56 seconds. The half-second sampler
recorded a maximum 5.33746 GiB RSS in that interval; this is a sampled stage
measurement, not GNU time's final whole-command peak. The edited spool contains
61,968,045,776 bytes. Graph updating, sorting, embedding, serialization and full
output validation remain outstanding at this boundary. Receipts:
`mapped_input/full-v4/rna-construction-boundary.json` and
`rna-construction-boundary-summary.json` in the same directory.

**V4 graph-update boundary, still nonterminal:** RNA logged completion of
transcript parsing and graph updating at Unix time 1789481869.190896. That stage
took 8,679.14 seconds (2h 24m 39s) and logged a cumulative high-water mark of
5.31598 GiB. Its completed spool held 35,576,539,824 bytes: a 16-byte header plus
4,447,067,476 eight-byte records, matching the retained raw graph's step count.
Matching counts do not prove path or graph identity. The command then entered
removal of non-transcribed regions; sorting/remapping, embedding, serialization
and full semantic validation remain. Sorting can append another set of spans
to this spool. Receipts are `mapped_input/full-v4/rna-graph-update-boundary.json`
and `rna-graph-update-summary.json` in the same directory. This absolute stage
measurement records the substantial runtime cost alongside the lower memory;
it is not a controlled speedup comparison.

**V4 sorting entry, still nonterminal:** RNA finished removing non-transcribed
regions in 1,018.87 seconds, with the logged cumulative high-water mark still
5.31598 GiB. Sorting began at Unix time 1789482888.065686. It computed an order
for 2,056,621 nodes, prepared 4,113,242 oriented translations, and reassigned
graph node IDs before entering completed-span rewriting at 1789482895.7422123.
Those preparation stages took 7.68 seconds together; this does not measure the
remaining step-wise rewrite. Embedding, serialization and terminal semantic
acceptance also remain pending. The phase timestamps and sampled memory are
retained in `mapped_input/full-v4/rna-sort-entry-summary.json`.

**V4 embedding entry, still nonterminal:** RNA completed sorting and compacting
2,056,621 nodes in 3,150.89 seconds (52m 31s), with the logged cumulative
high-water mark still 5.31598 GiB. It then entered transcript-path embedding.
The completed spool is 71,153,079,632 bytes: a 16-byte header plus two copies of
4,447,067,476 eight-byte step records. This is append accounting, not proof of
final path equivalence. Embedding, serialization and full semantic acceptance
remain pending. Receipts are `mapped_input/full-v5/rna-embedding-entry.json`
and `rna-embedding-entry-summary.json` in that directory.

An observed 44.06-minute window entirely inside embedding added 1,755,972 read
system calls and 1.093 GiB to `rchar`, while `write_bytes` increased by 71.437
GiB and `wchar`/`syscw` stayed unchanged. The spool uses `pread`; graph mutation
and checkpointing use mapped pages. These process counters establish continuing
activity and its I/O cost, not a physical-device write total, validated path
count, completion percentage, or ETA. Path lengths and possible read retries
prevent treating either bytes or syscall counts as an exact work fraction.
Snapshots and source hashes: `mapped_input/full-v5/rna-embedding-io-window-v1.json`.

**V4 path-release boundary, still nonterminal:** prune logged `Removed all
paths` at Unix time 1789481267.8268342. The interval after the original-graph
message took 6,853.37 seconds and includes both copying paths into XG and the
source-path release callback. It does not isolate their individual times.
Sampled peak RSS was 60.92205 GiB; sampled cumulative high-water mark was
60.92834 GiB. The first subsequent live observation had 26.55 GiB RSS, of which
only 0.0203 GiB was file-backed. XG construction, pruning/unfolding,
serialization and terminal equivalence checks remain pending. Receipts:
`mapped_input/full-v4/prune-path-release-boundary.json`,
`prune-path-release-summary.json` and `observations.jsonl` in that directory.

**V4 prune terminated by OOM:** the same invocation
`39321b20be8f43b4be7caa91ba980bb0` ended at 2026-09-15 14:41:28 UTC with
systemd `Result=oom-kill`, `MainPID=0` and a 62 GiB cgroup memory peak/limit.
The last periodic RSS sample was 66,194,833,408 bytes. The wrapper was stopped
without a final command status or GNU-time receipt, so this is a failed run,
not a completed RSS measurement. Its graph arena, seed mapping and logs remain
intact. No semantic checker is launched for the failed output. The terminal
service and source/library evidence is in
`mapped_input/full-v4/prune-oom-terminal.json`.

The next bounded correction removes an extra temporary copy in four XG
reverse-index allocations. `sdsl::util::assign(T&, const U&)` copy-constructs
`T(y)` from its const-reference argument. Direct assignment of the same
temporary instead invokes `int_vector::operator=(int_vector&&)`, which swaps
storage. Widths, values and occurrence order remain unchanged. The pinned build
passed with source/library immutability checks; its vg SHA256 is
`b87e06727827e4614e50960b6919884a1c3a2127cee5095e44adb72041a11299`.
A matched 134,217,728-entry packed-vector probe reduced peak RSS from
1,413,952 to 1,085,440 KiB with identical physical-vector hashes and no swap.
These receipts are under `xg_move_assignment_v1/build/` and `probe-run/`;
the probe is not a full XG/prune measurement. XG units, prune TAP, the dense XG
byte gate and small default/mapped pruning at 1/4/24 threads have passed. The
transcript-rich pruning fixture also passed graph/mapping byte identity, path
verification and graph validation at 7,848,176 KiB peak RSS, no swap and
16:08.49 wall time. All fixture receipts are in
`xg_move_assignment_v1/fixtures/`. Full-workload acceptance remains pending. The
completed V2 phase samples show peaks of 75.61 GiB during XG construction and
74.69 GiB after the unfolded-graph message, so later phases also remain part of
the acceptance contract. These phase samples are retained in
`xg_move_assignment_v1/v2-phase-reference.json`.

The waiting RNA V4 watcher was retired and replaced with
`rna_spool/watch_full_acceptance_v5.py` before the V5 prune launch, so admission
accounts for that prune service's remaining capacity. The RNA run, pinned
binary and validator destination remain at V4. Only the reserved mapped-prune
service name changes. The active watcher invocation is
`33e36c1ceae14ba9a3a5cbecc2995fd8`, with a 256 MiB/no-swap cap and CPUs 224-231.
Its source SHA256 is
`bc67a3caf0f0ffe087b152c86e0570db26dd4a54419813043fe253cfb0f4c630`;
12 unit and 5 negative tests passed. An initially stale unit-name expectation
and its failed receipt are retained, with corrected acceptance at
`xg_move_assignment_v1/watcher-preparation/acceptance-fixed.json`.

The fixed V5 prune terminal checker is prepared at
`chr21_prune/check_v5_terminal.py`. After the retained prune service terminates,
`chr21_prune/launch_v5_terminal_check.py` admits one separate 32 GiB/no-swap
validation service on CPUs 72-79, with a one-hour limit. Its conservative
244.25 GiB shared admission also reserves both RNA-related caps, 64 GiB shared
reserve and 64 GiB other growth. It refuses a live prune invocation, changed
invocation identity, existing destinations and insufficient resources.
It binds the V5 binary hash and invocation above. The older V4 checker remains
retained; the V4 OOM cannot pass its successful-command prerequisite.

The checker independently verifies GNU-time RSS/swap/exit status, fixed command
arguments and mapping seed, input stats and pinned tool hashes. An exact
control graph hash passes directly. An exact V2 output hash can reuse only the
checksum-bound, closed V2 graph/storage proof. An unfamiliar hash triggers the
existing pathless graph comparator; even a matching logical graph remains
`storage_review_pending=true` until its physical differences are reviewed.
The raw candidate acceptance is preserved. Thirteen synthetic tests exercise
field rejection, threshold boundaries, all three graph-proof branches,
comparator failure and mutation during validation. The live-run refusal check
created no validator outputs. Tests are in
`chr21_prune/check_v5_terminal_tests/`; live guard evidence is in
`mapped_input/full-v5/terminal-launch-guard/`. This validator has not
been launched while full prune remains active.

**Intermediate observation from the stopped V3 attempt, not final acceptance:** the mapped RNA
run completed transcript parsing and graph updating at 10:01:27 UTC on
2026-09-15. That stage took 3,829.05 seconds and reported a cumulative 5.19319 GiB
high-water mark, against the retained control's 635.702 seconds and 78.5666 GiB
at the same boundary. All 5,607,688 reference transcript paths were constructed;
the completed-handle spool reached 33.13 GiB. Later stages and semantic
validation remain pending. The direct-XG prune candidate's timestamp-grouped
samples place its highest observed RSS, about 75.6 GiB, between path release
and completed XG construction; its complement counts match the retained control.
These observations neither establish final peaks nor identify every live
allocation. Receipt: `phase_summary/prune-v2-unfolding-snapshot.json`, alongside
the full runs' `measured/phases.jsonl` and `measured/rss.jsonl`.

`check_prune_fixture.py` reuses successful control command receipts, starts a
fresh explicit mapping seed, and requires candidate graph/mapping byte identity,
successful path verification, and graph validation. It preserves every attempt.

`TranscriptPathSpool` stores 8- or 16-byte step records behind 64-bit file spans,
uses checked positional reads and writes, and retains its exclusively created
file after success or failure. Its span, truncation, and existing-file tests
passed in the second vg build. This scratch format makes no resume promise.

`path_graph_fingerprint` canonicalizes nodes by their first occurrence in
lexicographically ordered named paths, including strand, and hashes canonical
sequences, edges, and full named walks. Its memory beyond the loaded graph is
proportional to nodes, edges, and path names, not total path steps. It rejects
uncovered nodes, so its intended input is the raw `vg rna -d` output. Microchecks
accepted sparse-ID/strand relabeling and rejected altered edges, altered walks,
and uncovered nodes. The real RNA fixture fingerprint took 1.33 seconds and
71,680 KiB. Empty/circular-path importer limitations are recorded in
`fingerprint_micro/acceptance.json`; those features are not part of the supplied
RNA recipe. The original read-only full exact chr21 fingerprint attempt was
stopped before producing a result, with partial evidence retained in
`chr21_rna_fingerprint/replacement-stop.json`. Its helper Makefile omitted
optimization. The replacement helper uses explicit `-O3` and stderr progress
messages; its hashes match on every retained micrograph and the real fixture
(0.23 seconds, 69,632 KiB). The replacement baseline fingerprint completed under
a separate 64 GiB cap: 11:54.90 wall, 44,105,108 KiB peak RSS, zero swap,
exit 0, unchanged input size/mtime. Its 2,056,621 nodes, 2,726,485 edges and
5,607,688 named paths and three semantic hashes are recorded in
`chr21_rna_fingerprint_v2/acceptance.json`. This is a graph
validation scan; neither completed RNA nor prune control was rerun.

`check_chr21_rna_semantics.py` consumes that terminal baseline receipt and runs
only the candidate's graph validation and fingerprint. Its caller must supply
a separate resource-limited service. It rechecks recorded input stats and
transcript-info bytes, compares all six fingerprint fields, and requires both
runner and command success. The stale-input negative check in
`semantic_gate_stale_input_negative/` fails before starting either child.

The old fixed-purpose watcher in `rna_spool/watch_full_acceptance.py` ran as
`vg-memory-chr21-rna-validation-watch-v1-20260915`, capped at 256 MiB with no
swap. It waits for the current RNA invocation and terminal command receipts,
then rechecks inputs and pinned tools and admits one separate 64 GiB semantic
service. Admission reserves 64 GiB for other growth and 64 GiB shared reserve,
plus the unused capacity of **both** active prune services. It rechecks memory
after hashing and waits again if the budget has tightened. It never retries or
restarts construction. Successful transient units may be collected by systemd;
their synthetic `not-found` success defaults are not completion evidence.
The new validator retains `active/exited` state, and watcher acceptance also
requires `MainPID=0`, command success, immutable inputs and a passing semantic
receipt. Seventeen mocked checks passed in `rna_spool/watcher-tests/*-v4/`.
Retained old watcher receipts are under `rna_spool/chr21-validation-watch-v1/`;
its proposed semantic output was `rna_spool/chr21-candidate-v2-semantics/`.
After intentional V3 retirement it failed its missing-terminal-receipt gate
and did not launch a semantic validator. Its final failure is retained.
The old watcher SHA256 is
`1208135e75674beafd08fbefcdd218099335ebd32cf14e4e636ea2a6181a28ee`.

The active V4 watcher is `rna_spool/watch_full_acceptance_v4.py`, SHA256
`cda8b5b314e928bf8e52fef49eeafeddc9b7273c579b429c10280028bdd554bc`,
in `vg-memory-chr21-rna-validation-watch-v4-20260915`. Only fixed candidate,
binary and service names/hashes changed; all 17 mocked checks passed again.
Its receipts are in `rna_spool/chr21-validation-watch-v4/`, with eventual
semantic output in `rna_spool/chr21-candidate-v4-semantics/`. The same separate
64 GiB validation limit and post-hash resource gate apply.

A pinned 24-thread RNA fixture control completed at 689,384 KiB and 34.48
seconds. Its canonical fingerprint exactly matches the one-thread control;
see `fingerprint_micro/thread-comparison.json`. This establishes the fixture's
existing thread-order behavior before testing the mapped/spooled route.
Their transcript-info files also match byte-for-byte. `check_rna_fixture.py`
requires semantic graph/path equality, transcript-info byte identity, graph
validation, and one-thread graph byte identity, with only the explicit mapped
backend exception described above. Workspace-enabled runs must retain their
new workspace. It refuses controls larger than 1 GiB so that the
fixture runner cannot silently become a full-workload validation job.

`run_measured.py` records command arguments, raw stderr, timestamped phase
messages, sampled executable RSS, GNU time, and an explicit terminal status in
a new output directory. It must run inside a resource-limited service; it is not
a resource limiter itself. GNU time determines acceptance, since sampling can
miss brief peaks. A killed service may leave no terminal status; retain its
service result and partial files.

`summarize_measured.py MEASURED_DIR` groups samples between timestamped log
messages, including time before the first message. It reports sampled RSS,
anonymous/file RSS and process high-water marks separately from GNU time's
completed peak. A missing terminal receipt means unknown liveness, not a running
or successful job. The helper passed real completed-fixture and active-receipt
checks, plus stopped/partial and corrupt-interior JSONL cases, under
`phase_summary/`. These intervals locate observations; they do not by themselves
identify the allocation that caused a peak. Live CPU/I/O/workspace snapshots and
cgroup reclaim/OOM counters are retained in `mapped_input/full-run-snapshots.jsonl`
and `mapped_input/full-run-cgroup-snapshots.jsonl`.
Subsequent consistent snapshots use `mapped_input/full-run-monitor-v2.jsonl`,
with explicit GiB field names and retained per-service OOM counters.

Resource admission uses shared `user.slice` nonreclaimable memory, with a
64 GiB reserve, plus the existing 2 TiB SSD floor. The initial RNA fixture ran
under a 16 GiB limit with swap disabled. The build uses a separate 48 GiB limit.

On this host, the user slice delegates `cpu memory pids`, but not `cpuset`.
Thus the earlier build/fixture services' `AllowedCPUs` settings were configured
without enforced affinity; their wall times do not establish a controlled
speedup. The full prune V2 command has explicit `taskset` binding (verified
232-255). New services prefix their payload with `taskset`, and new measurement
receipts record the runner's actual affinity. MemoryMax remains enforced via
the delegated memory controller. Details: `mapped_input/resource-binding-note.json`.

The mapped prune candidate still needs its external cgroup cap for a hard RSS
bound. XG's per-path callbacks periodically evict the mapped input, but the
separate `destroy_all_paths` callback checkpoints only after
`BasePackedGraph::destroy_paths` returns. That bulk implementation walks every
selected path step and scans each visited node's membership list once; it has
no special branch that skips the step traversal when all paths are selected.
The exact input has 4,238,280,959 active steps. This is a code-derived traversal
model, not a sampled call-stack attribution. During this uncheckpointed bulk
call, mapped pages may accumulate in RSS; `--path-workspace` alone is not a
256 MiB RSS guarantee. The full-run 62 GiB cgroup cap remains part of the
reproducible configuration. A live mapping inventory found the 128 GiB graph
arena as the only large data-file mapping, besides the executable and shared
libraries: `mapped_input/full-v4/prune-file-mappings.json`. Its virtual extent
is not its resident size.

During V4 edited-path spooling, a ten-second read-only thread sample found RNA's
active writer in uninterruptible sleep in all 20 samples, with
`balance_dirty_pages` reported in 19. Prune's active thread was running in all
20 samples. The service and ancestor CPU quotas were unlimited, with zero CPU
throttling counters. The spool and graph arenas are on `/mnt/ssd` (ext4 on
`/dev/sdb1`). At this observation, host `vm.dirty_bytes` was 100,000,000,
`dirty_ratio` 0 and `dirty_background_ratio` 10. This records a writeback cost
and host configuration; it does not apportion total runtime or establish the
effect of changing that configuration. Global settings and running jobs were
left unchanged. Evidence is in `mapped_input/full-v4/`:
`io-observations.jsonl`, `scheduler-observation.json`, `io-thread-sample.json`
and `writeback-observation.json`. The last receipt distinguishes unavailable
leaf I/O-controller counters from a failed run. Both services had zero OOM
events; file-cache reclaim events under their caps are recorded separately.

The option checker accepts RNA and prune. Writable-output checks deliberately
run after RNA's workspace-option rejection to preserve existing requested
outputs; the seven early-rejection tests verify that behavior. The checker now
recognizes narrowly formatted post-switch checks of the same filename variable,
including its nonempty guard. It retains the original assignment heuristic and
declines the exemption across ambiguous block comments or multiline strings.
Six focused Python regression tests pass. Comparing the HEAD and updated
checker on the same current C++ tree removes exactly the four RNA false
positives and adds no diagnostics. Repository-wide lint still exits 1 for
unchanged diagnostics in the GCSA worker, index, and autoindex commands.
The checker copies, stdout, hashes, and targeted acceptance receipt are retained
in `mapped_input/options-lint-v4/`; the earlier diagnostic log remains at
`mapped_input/options-lint-v3.txt`. This Python-only change requires no rebuild
of the binaries used by the active full runs.

## Mapped metadata scaling and allocator correction

The full mapped prune run remained in input loading for over 80 minutes after
its reads reached the end of the ordinary PackedGraph input. A separate bounded
probe gives each distinct 96-character path name one step on the same single
node. Ordinary and mapped construction use the same insertion sequence; the
ordinary graph is destroyed before mapped construction starts. Timings exclude
final verification and destruction.

| named paths | ordinary cumulative seconds | mapped cumulative seconds |
|---:|---:|---:|
| 1,000 | 0.01965 | 0.03321 |
| 10,000 | 0.21379 | 1.11866 |
| 25,000 | 0.52334 | 10.4653 |
| 50,000 | 1.07033 | 42.3528 |
| 100,000 | 2.13839 | 123.334 |
| 200,000 | 4.28771 | 405.710 |
| 500,000 | 10.6914 | not reached |

The ordinary case completed its count, name-length, enumeration and one-step
checks. The mapped case reached the fixed ten-minute service limit before
500,000 paths or final verification: this is an **incomplete timeout**, not a
completed mapped benchmark. The service used a 4 GiB cap, no swap and CPUs
64-65, after fresh admission. Its reported 520.7 MiB memory peak is aggregate
cgroup memory, not process RSS. Source, command, admission, live checkpoints
and terminal receipts are in `mapped_input/path_name_scaling/v2/`.
The first attempt's buffered output yielded no usable curve and is preserved
in `mapped_input/path_name_scaling/run/`.

This exposes superlinear metadata/allocation cost in the small construction
probe. It does not prove the inverse loader's exact bottleneck: the loader
rebuilds its name index after many other allocations. Allocator/access-path
diagnosis led to the allocator correction below. These observations came from
the superseded V3 attempts; their pinned binaries and partial workspaces remain.

Source inspection and a read-only parser of that retained arena identify a
specific leading cause: YOMO's address-ordered free list contains 61,144 blocks.
First-fit searches for representative 576/864/1,152-byte allocations traverse
1,687/6,445/13,649 blocks; freeing also starts at the list head and performs
chain-position lookups at each hop. This is deterministic final-state evidence,
not a dynamic profile of every allocation. The candidate fix adds ephemeral
per-chain indexes by offset and by size under the existing allocator lock,
while retaining the persisted free list as authority. It selects best-fit blocks
with an offset tie-breaker and drops the optional indexes on cache-insertion
failure or mismatch. Native persisted layouts are unchanged. The complete
libbdsg suite passed at 877,168 KiB and 47.64 seconds, including fragmented
native reopen/copy, cross-link allocation and four-thread allocation/free tests.
Receipts are in `mapped_input/allocator-candidate-v1/`. Diagnostic source and receipts are under
`mapped_input/path_name_scaling/allocator_diagnosis_v1/`.

A separate helper injected scalar `operator new` failures during lazy cache
creation, split-remainder indexing and free/coalesce indexing. Each operation
completed and subsequent allocator operations, heap integrity and chain cleanup
passed. It skips one compiler-specific `std::function` wrapper allocation to
reach the intended cache allocation; this does not test arbitrary allocation
failure sites. Failed interception attempts and final success are retained in
`mapped_input/allocator_fault_v1/`. No production hooks or source edits were
needed for this test.

The identical 500,000-path probe then completed both semantic verifications at
780,280 KiB peak process RSS, 6:41.96 overall, with no swap. Mapped construction
checkpoints were 16.1417/38.8873/124.581/388.618 seconds at
50k/100k/200k/500k paths. The last two intervals cost about 0.857/0.880 ms per
path. The old probe's 200k checkpoint was 405.710 seconds and its full mapped
verification never completed, so this is not a completed end-to-end old/new
speedup measurement. Source, command and terminal receipts are in
`mapped_input/path_name_scaling/v3/`.

An independent workspace-only RNA change is in the integrated candidate:
`sort_compact_nodes()` makes translations for both orientations of every node in
the computed graph order. It skips the completed-spool discovery scan that
formerly made one hash insertion attempt per transcript step. This removes one
33.133 GiB input scan and approximately 4.447 billion insert attempts on the
full exact chr21 workload, in exchange for at most twice the node count in
translation entries. The ordinary route is unchanged. Existing graph/path
rewrite and semantic acceptance checks still apply. The integrated build
completed in `mapped_input/build-integrated-v3/` under a 48 GiB cap and CPUs
32-63. Both build stages exited zero; source and live shared-library hashes
were unchanged. The pinned binary SHA256 is
`aed50ea90bb2aa8f3311a628b4df54ea2bfdc3241a54cc6e4a98a0c8fa3c570d`.
Compilation took 20:23.46 wall; late compiler stalls were traced to temporary
assembly files on the system disk under `/tmp`. The build finished before a
proposed SSD-temporary-directory retry, so no build or scientific job was
stopped for that issue. Future builds should use a dedicated SSD `TMPDIR`.
New progress messages separate graph ordering, translation preparation and
completed-spool rewriting. The complete fixture suite passed in
`mapped_input/acceptance-v3/` under a 24 GiB cap, no swap and CPUs 80-103:
unit tests, prune TAP, option rejection, tiny prune at 1/4/24 threads, default
RNA byte identity, mapped RNA at 1/24 threads, and transcript-rich prune
graph/mapping byte identity and retention verification. GNU-time results are
629,092 KiB / 47.05 seconds for default RNA, 550,104 KiB / 3:56.42 for mapped
RNA at one thread, 550,524 KiB / 5:31.27 at 24 threads, and 7,845,600 KiB /
17:44.34 for mapped transcript-rich prune. The mapped route remains slower on
these fixtures; host conditions differ between retained attempts, so these
are absolute measurements, not controlled speedup estimates. RNA V4 was stopped
after runtime rejection; its scratch was removed under the user's cleanup request.
It has no completed memory or semantic acceptance. Mapped prune V4
terminated by OOM; its preserved failure and follow-up copy correction are
recorded above.
During rewriting, `completed.steps` grows by appending
replacement spans, so current physical file size is not the live step count.

The transcript-rich prune fixture also retains the full command's node-mapping
seed of 1,000,000,000. Its control contains only 197,800 nodes but spans IDs
1 through 1,000,005,103 and occupies 2,709,972,906 bytes. In
`BasePackedGraph::new_node_record`, `nid_to_graph_iv` fills the entire intervening
ID interval with zeros. This is a separate representation cost from path-step
amplification; it prevents extrapolating the fixture's runtime from transcript
count alone. Read-only header evidence is in
`mapped_input/acceptance-v3-seeded-id-span.json`. This establishes the ID-span
cost, not a dynamic attribution of every sampled writeback wait.
