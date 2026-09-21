# Transcript-rich graph memory attribution

Investigation: 2026-09-15. Scope: unchanged transcript/path workload in `vg rna`
and `vg prune -u`. This is the requested first deliverable, not an optimization
benchmark result or a production launch.

Active implementation, acceptance targets, and validation receipts are tracked
in [IMPLEMENTATION.md](IMPLEMENTATION.md).

**Current validation routing (2026-09-16 UTC):** the parallel V3 chr21 RNA
result is terminal accepted at 33:31.11, 18.538162 GiB/1.30615977x OR and zero
swap. Its independent checker is terminal success: raw graph bytes differ from
the preserved serial V3, while `vg validate`, all six canonical graph/path
fields, exact info bytes and evidence guards pass. The serial 54:11.56/
18.5631 GiB result remains the previous accepted baseline. The isolated metadata
pilot is terminal PASS. Its resulting private lookup-removal gate is also
terminal PASS: `rna_metadata_lookup_v1/checks/units/stdout` reports 42,622
assertions in 14 cases, while `rna_metadata_lookup_v1/checks/acceptance.json`
binds exact retained 100,000-name bytes at T1/T24
and a 5,607,688-name CPU-only ABBA reducing mean whole-writer wall
from 93.4301 to 77.5504 seconds (17.0%) without increased peak RSS. This accepts
a private prototype, not an integrated `vg` or full-RNA speedup. Its apply-ready
patch remains unapplied until the active prune command and terminal check release
the production-source/library freeze; a guarded build and fixtures follow. A
whole-RNA rerun is unnecessary absent a new integration concern.
Ordinary prune V2 already proves the required `<2x OR` target at
75.608 GiB, but only for its pinned older binary. The frozen current binary is
therefore undergoing one ordinary-default provenance revalidation under
`tmp/transcript_memory_20260915/prune_current_binary_v1/`. Its fixture gate is
terminal PASS: TAP 26 and ordinary T1/T4/T24 plus real T24 have exact graph and
mapping bytes and validation with stable guards. The full command is live as
invocation `be246266875c49e48228747eebedad77` under 96 GiB/no swap and a six-hour
deadline; it has loaded 2,056,621 nodes and 2,726,485 edges, and no semantic
checker has launched. Its guards cover all 574 source files and libhandlegraph;
production sources and build libraries remain frozen through its terminal gate,
and later RNA prototypes must be isolated from them. This is not a mapped retry or a
speculative attempt at the unresolved 62.879 GiB 1.5x target. The controlled
synthetic XG ABBA is not a whole-prune claim. This attribution report below
remains historical evidence.

## Findings and confidence

1. **Measured from retained RNA logs:** the exact-only run reaches a new high-water
   mark during path embedding, from 78.5666 to 99.1276 GiB. Its completed transcript
   handle vectors remain live while all paths are embedded in PackedGraph.
2. **New file-header measurement:** exact-only has 4,447,067,476 raw embedded steps
   and 4,238,280,959 biological steps after retention-path removal. OR has
   586,975,164 and 538,881,605 respectively. Biological steps grow **7.865x**,
   whereas representatives grow 5.345x; average steps per biological path also grow.
3. **Leading prune hypothesis, supported by exact source allocation sizes:**
   temporary XG construction creates a 126.37 GiB mapped occurrence file and
   94.73–94.78 GiB of initially 64-bit reverse-index arrays for exact-only, while
   retaining the input PackedGraph and already-built XG paths. This is a much
   more specific candidate than attributing all 256.70 GiB to PhaseUnfolder.
4. **Not measured:** which instant owns prune's maximum RSS, how much of the mapped
   file is resident at that instant, and the split between live heap, allocator
   retention, and file-backed RSS. The closed prune receipts contain GNU-time
   maxima and untimestamped phase logs, not a phase RSS/heap trace. Do not turn the
   allocation model into a measured decomposition of the 256.70 GiB total.

**First implementation candidate:** allocate XG reverse-index arrays at widths
computed from their actual maxima, instead of 64 bits followed by compression.
The exact-only input bounds imply **at least 62.66 GiB less logical array storage
at that construction phase**. This is an allocation saving, not a promised
62.66 GiB reduction in whole-command RSS. Instrument and test on bounded fixtures
before making that performance claim.

## Provenance and corrected comparison

- Fork: `rna-copy-elimination`, HEAD `9a377ca9ca41d6aa71c33add6fa2038d3b95c1b3`.
  Relevant source files in Transcriptome, prune, PhaseUnfolder, XG and libbdsg had
  no uncommitted changes when inspected. Existing unrelated edits are preserved.
- Local `bin/vg` and the research-pinned binary both hash to
  `35867c7faa54fd4135f16dc46ad58c9e25a2faaf33079afabf4f3ba2f937a273`.
  See [binary receipt](binary_sha256.txt). The benchmark binary predates the
  documentation HEAD but contains the merged RNA compact-step/lifetime/scoped
  augment-bypass work; the retained chr20 gate explicitly names this hash.
- Research root: `/mnt/ssd/lalli/hprc_v2_vg_rna`.
  `WORKSPACE_STATE.md` Sections 17, 19 and **superseding Section 21** were read.
  Section 21 corrects the older note's controlled-comparison wording.
- Closed inputs, outputs and receipts:
  `/mnt/ssd/lalli/hprc_v2_vg_rna/notes/evidence/chr21_exact_arm_20260914/exact/` for the
  exact arm, relocated there 2026-09-20 from an ephemeral job directory, and
  `/mnt/ssd/lalli/.claude/jobs/0c05a913/tmp/chr21_downstream/or/` for the OR arm, which
  is still in that job directory and therefore still at risk.
  Commands were checked against `rna_arm.sh`, `prune_arm.sh`, and the per-command
  `*.time.txt` receipts. Both arms completed through GCSA, but they compare OR
  with exact-only, **not AND**.
- RNA: 24 threads in both arms. Prune: 48 OR / 24 exact-only. GCSA: 32 / 24.
  Prune/GCSA timing ratios do not isolate selection. Their memory ratios are
  observations under these configurations, not a matched-thread causal estimate.
- Recipe: RNA `-r -d`, 1 kb retention features; prune `-u -a -k 32 -M 0`, guide
  GBWT, fresh seed mapping. Guide paths were captured before retention-path
  removal. Removing guide protection is outside equivalent optimization scope.
- The ~1.2 TiB chr2 figure is an extrapolation. It is not an observed run or a
  lower bound on an improved implementation.

## Measurements obtained without rebuilding graphs

The [header probe](inspect_packed_header.py) reads libbdsg's serialization headers
and six trailing counters. It skips vector payloads and stops at the membership
count. One membership slot represents one allocated step; subtracting deleted
membership records gives live steps. This matters because stripping pad paths
left tombstones in both large graphs.

| Quantity | OR | Exact-only |
|---|---:|---:|
| Representatives (retained evidence) | 262,137 | 1,401,105 |
| Raw paths, including retention | 1,051,816 | 5,607,688 |
| Biological paths after strip | 526,239 | 2,804,175 |
| Raw live steps | 586,975,164 | 4,447,067,476 |
| Biological live steps | 538,881,605 | 4,238,280,959 |
| Deleted retention-step records still allocated | 48,093,559 | 208,786,517 |
| Mean steps per biological path | 1,024.02 | 1,511.42 |
| Raw PG bytes per live step, whole-file ratio | 9.01 | 8.58 |
| Stripped PG bytes per live step, whole-file ratio | 9.34 | 8.75 |
| Longest raw path, bases | 1,137,549 | 1,137,549 |
| PackedGraph node count | 1,796,032 | 2,056,621 |
| Graph sequence bases | 34,061,470 | 34,459,835 |

The PG ratios include topology, names, padding and deleted records; they are
**serialized bytes per step, not resident heap bytes per step**. The base difference
is the retained 398,365 bp from the original experiment. The stripped names lists
contain no `_alt_`-prefixed paths.

Probe evidence: [headers](packed_headers.jsonl), [resource receipt](header_probe.time.txt).
Four graphs took 1.37 s and 10,240 KiB peak RSS under a 256 MiB address-space bound
and 45 s timeout. It explicitly read 3,010,812 header bytes; GNU time recorded
273 MiB filesystem input, including block/page-level reads. It did not deserialize
the 85.5 GB of graph files or scan billions of steps.

Validation: node counts and sequence totals agree with retained graph statistics.
A tiny GFA with repeated/reversed visits has five steps; deleting one named path
leaves three, with both graph nodes and all three bases preserved. The probe returns
those expected counts ([fixture receipt](probe_validation.jsonl)). A separate
tombstone fixture checks subtraction before defragmentation. This is not a fresh
checksum or semantic validation of the large graphs.

A separate 11.98 s, 2 MiB-RSS scan of existing `raw_info.tsv` obtained path counts,
total path lengths and the maximum length: [lengths](path_lengths.txt),
[resource receipt](info_scan.time.txt). Values in [allocation_model.json](allocation_model.json)
are computed from these measurements and the source layouts below.

## Phase attribution: `vg prune -u`

Let `S` be biological live steps, `N` nodes, `P` biological paths, and `U <= N`
nodes with no biological step. Retention-only nodes can contribute to `U`.

| Phase | Simultaneously live owners | Attribution / uncertainty |
|---|---|---|
| Load `genic.pg` | Mutable PackedGraph: topology, sequences, path links/steps, reverse memberships, name tables and path objects | Exact serialized size 34.52 GiB (OR 4.69). Resident size additionally includes object/capacity/hash overhead. No current live-heap measurement. |
| Build temporary XG paths | Original PackedGraph + XG topology + every completed `XGPath`; one currently materialized path | XG duplicates the read-only path representation. Per-path scratch includes `8*steps` handles and a temporary bit per path base. The latter is at most about 139 KiB for the observed maximum path length, not a chromosome-wide bitmap. |
| Build XG node-to-path occurrence index | All preceding objects + mapped occurrence file + reverse-index arrays | Dominant candidate, quantified below. Mapped pages are file-backed RSS, not jemalloc live allocations. |
| Delete paths from mutable graph | Completed XG + mutable graph while batched path deletion releases steps and defragments memberships | Path data stays until **after** XG construction. Deletion does not explain away the earlier overlap; allocator release need not immediately reduce RSS. Path-name/per-path skeleton storage also survives. |
| Prune complexity / small components | Completed XG + pathless mutable graph + enumeration/removal scratch | Driven by topology and search work; `-M 0` disables the optional high-degree prepass. Cannot assign measured RSS from available logs. |
| Load guide / form complement | Completed XG + mutable graph + GBWT + complement HashGraph and component copies | Guide is loaded only now, after XG and pruning. Exact complement: 512,115 nodes / 658,609 edges / 25,740 components. Serialized guide is 611.7 MB, not its measured heap size. |
| Unfold | Above indexes + all components + growing unfolded HashGraph + concurrent local tries/reference walks/stacks/mappings + batch operation logs | Batch size is `8*T`: 192 component logs at T=24 versus 384 at T=48. Component count is bounded, bytes are not. Reference walks can repeat per border occurrence. Potential secondary peak requiring profiling. |
| Replay / extend / serialize | Local/global mapping and rebase arrays; unfolded HashGraph overlaps destination while copied; XG remains in scope | Exact unfolded graph: 6,077,286 nodes / 6,185,993 edges / 216,871 unique crossing paths. Final: 7,621,792 nodes / 8,242,163 edges. Counts do not quantify retained heap. |

Code anchors: `src/subcommand/prune_main.cpp:343-390,431-467`;
`deps/xg/src/xg.cpp:811,1080,1445`; `src/phase_unfolder.cpp:19-70,331-414,451-584`.

### Quantifying the XG overlap

`XG::index_node_to_path()` writes one `(node rank, path/orientation, step rank,
base position)` record per visit. Four 64-bit fields cost **32 bytes per step**;
`mmmulti::padsort()` adds `N+1` null records. It maps and sorts the whole file.
That file remains mapped while XG allocates `np_iv`, `nr_iv` and `nx_iv` as
`sdsl::int_vector<>(S+U)` with the default 64-bit width. The three arrays cost
**24 bytes per entry**, plus a bitvector. Only after copying entries does it
unmap the occurrence file and bit-compress the arrays.

| Code-sized allocation | OR | Exact-only |
|---|---:|---:|
| Mapped occurrence file, `32*(S+N+1)` | 16.11 GiB | 126.37 GiB |
| Three initial arrays, `24*(S+U)` | 12.04–12.09 GiB | 94.73–94.78 GiB |
| Occurrence-start bitvector, approximately `(S+U)/8` | 0.063 GiB | 0.494 GiB |

The roughly **221.1 GiB** occurrence-file-plus-array footprint in exact-only exists
on top of the input graph and XG paths. Not all mapped pages must be resident
together; adding these to file sizes is not an RSS reconstruction. Nevertheless,
the sizes identify a concrete mechanism large enough to explain the observed
peak. `mmmulti`'s sort is in-place; do not invent another full record-copy array.
SDSL's late `bit_compress` is also in-place, though allocator retention remains
possible after shrinking.

References: `deps/xg/src/xg.cpp:1445-1559`;
`deps/xg/deps/mmmulti/src/mmmultimap.hpp:236-316`;
`deps/sdsl-lite/include/sdsl/util.hpp:403`.

## Phase attribution: `vg rna`

The `GB` label in RNA progress comes from `gcsa::inGigabytes`, using powers of
1024. Its `memoryUsage()` returns `ru_maxrss`: these are cumulative high-water
marks, **not live memory at each logged boundary**.

| Phase | OR HWM | Exact HWM | Live owners / interpretation |
|---|---:|---:|---|
| Parse GBZ / convert to PackedGraph | 0.60 GiB | 0.60 GiB | GBZ/GBWT and mutable topology overlap during conversion; GBWT is moved into its longer-lived index. This is not the large measured peak. |
| Parse annotations, construct edited paths, split nodes, complete paths | 11.74 | 78.57 | `vector<Transcript>` including exon arrays; all edited paths; per-thread full extracted haplotype fragments and incomplete/per-contig transcript lists; breakpoint/index state; completed handle paths. The edited-to-completed transition already drains edited paths and uses moves. Which subphase sets 78.57 is unmeasured. |
| Remove nontranscribed nodes | 11.74 | 78.57 | Completed paths determine retained nodes; original embedded paths removed; transcribed-node set and deletion list. Unchanged HWM does not mean 78.57 GiB remains live. |
| Sort/compact IDs | 11.74 | 78.57 | Topological order, orientation-aware update map, graph remapping and completed paths. Each worker temporarily creates one replacement path vector, not a second copy of all paths. |
| Embed transcript paths | **14.19** | **99.13** | All `_transcript_paths` handle vectors coexist with the growing graph's embedded steps, links and reverse memberships. This phase sets the whole-command peak in both retained logs. |
| Write info / graph | 14.19 | 99.13 | Completed vectors still exist: info computes lengths by traversing them. No new HWM. This command does not build transcript-output GBWT (`-b`) or haplotype-output GBWT (`-v`), nor use the optional `-B` body-path writer. |

Per-step layouts and lifetime costs:

- Edited paths already use `EditedMapping { handle, offset, length }`: **16 B per
  pre-split step**, plus vector capacity. The old ~190 B protobuf step and whole
  transcript-route translation materialization are removed. Do not propose them
  again. The historical chrY heap percentages describe the old binary.
- Completed paths use **8 B per final step**, plus capacity. The exact raw graph's
  final steps imply **33.13 GiB minimum handle payload** (OR 4.37 GiB), still live
  through embedding and info output. Pre-split edited step count/capacity is not
  available from the final artifact; do not equate it with final step count.
- Each PackedGraph embedded step stores six compressed integer fields: traversal,
  previous and next step, reverse-membership path ID, step offset, and next
  membership. Add per-node membership heads and per-path names, headers, indexes,
  pages and capacity slack. These six fields are **not six uncompressed uint64s**.
- `_transcript_paths` includes separate identities for exon/body/pad models in
  this supplied annotation. Sharing storage may preserve those identities;
  dropping models or changing their grouping does not preserve this workload.
- Full haplotype extraction is per active worker/fragment, `8*sum(fragment_steps)`;
  edited path ownership moves through worker lists into the global list. The code
  does not make T full copies of the entire transcriptome.
- Copy-returning transcript subset accessors exist, but this command does not
  invoke them. The renamed/sorted paths are moved, not deeply copied wholesale.
- Live heap versus allocator retained/resident memory is unresolved. `clear()`
  or destructor evidence alone does not quantify RSS recovered. For jemalloc,
  sample `allocated`, `active`, `resident`, `mapped`, `retained`, and dirty/muzzy
  page state alongside process anonymous/file RSS. Retained virtual mappings
  are not automatically resident pages.

Anchors: `src/transcriptome.hpp:157-216`; `src/transcriptome.cpp:337-425,1283-1535,
2132-2475,2548-2605,2736-2933,3168-3203`; `src/subcommand/rna_main.cpp:320-344,
445-520,545-615`; `deps/libbdsg/bdsg/include/bdsg/internal/base_packed_graph.hpp:
594-703,2690-2770,2804-2847`.

## Ranked changes, savings and tradeoffs

### 1. Allocate XG reverse arrays at their final widths

Collect maximum encoded path/orientation, step rank and position while producing
the occurrence records. Allocate the three arrays with those bit widths, zero
initialized; preserve entry order, null-node padding and final serialization.
Keep 64-bit **counts and offsets**: this input has over 2^32 raw steps.

For these inputs, every step is at least one base, so the measured longest path
of 1,137,549 bp bounds step rank and position to 21 bits. Path/orientation needs
at most 21 bits for OR, 23 for exact-only. Therefore array storage falls from
24 B/entry to at most 7.875 / 8.125 B/entry: **8.09 / 62.66 GiB saved** at the
array-allocation phase. Actual maxima may permit narrower arrays.

This adds constant work to an existing traversal, no extra disk pass or
temporary file, and avoids late wide-array scans/compression. Packed writes can
have different CPU costs; measure them. If mmap sorting or unfolding owns the
global peak, global RSS reduction can be smaller or zero despite the array
saving. This is the first narrow experiment, not a guaranteed solution by itself.

### 2. Remove or bound XG's occurrence-file materialization

Reuse the input graph's reverse membership API to construct node-indexed arrays,
sorting occurrences for one node at a time into **the same order XG currently
uses**, or implement a memory-budgeted spill sort. A node with very high path
depth requires its own bounded fallback. This can remove the **126.37 GiB file**
and its write/sort/read traffic; the RSS benefit depends on residency. Do not add
this saving mechanically to item 1's whole-process RSS prediction.

This is larger work than width sizing. XG's occurrence ordering affects
PhaseUnfolder's first-match reference extension and duplicate-ID assignment.
Passing PackedGraph directly as `path_graph`, or replacing reference paths with
only the GBWT guide, is not established byte-equivalent. Preserve ordering and
both protection sources explicitly. Changing the representation need not change
public graph/path semantics or retained sequence.

### 3. Stop overlapping completed RNA handles with embedded storage

For the current `-r -i` output route, consume each completed handle vector after
embedding and satisfying its last output consumer. Options: emit that path's info
in the same sorted order during consumption, or retain an embedded path handle
and read it for later outputs. Optional `-B`, `-b`, `-f`, haplotype-only and
nonembedded paths need their original behavior/fallback; releasing vectors
immediately after embedding without handling those consumers is incorrect.

This targets **33.13 GiB of handle payload**, plus slack, at exact-only embedding.
But leaving earlier phases untouched leaves the **78.57 GiB prior HWM**, so the
best whole-run saving from an embedding-only change is **20.56 GiB (20.7%)**.
Earlier consumption cannot lower a peak that already occurred. Traversing
PackedGraph for later FASTA/GBWT output may be slower than contiguous vectors;
streaming existing info in order avoids an extra pass for this recipe.

### 4. Bound RNA construction and unfold state after profiling

- RNA: flatten/share identical walk storage while retaining all named models;
  tighten measured vector capacity; compact finalized handles where IDs permit;
  release parsed metadata earlier. Larger improvement: global breakpoint discovery
  followed by bounded replay/spill of edited paths. Node-splitting and output order
  must stay identical at T=1; independently augmenting batches can change IDs.
- Unfold: admit component logs by bytes, not `8*T` component count; stream/spill
  ordered logs; share repeated reference walks with stable encounter order; move
  the worker mapping rather than copy it. Release completed component state after
  replay. Savings depend on measured largest components, not transcript count.
- PackedGraph append-only/frozen path storage could omit mutable links or share
  walks, while maintaining reverse membership and public interfaces. This is a
  deeper representation change; first measure `report_memory()` categories.
- Allocator purging can diagnose cached pages, but should be measured separately
  and not presented as removing live path data. It can increase page faults and
  runtime. Do not change allocator policy in the same A/B as a representation fix.

The hybrid exact-topology/AND-path experiment, stronger transcript deduplication,
guide pruning and annotation reassignment are **separate scientific policy work**.
None is a substitute for representing this exact workload more cheaply.

## Bounded implementation and acceptance experiment

### A. Instrument one capped diagnostic fixture

Use new task-owned outputs and matched builds. Pin the existing control outside
build paths; compile any diagnostic/candidate binary using `./build-local.sh`,
never rebuild/overwrite the research-pinned executable. Preserve all completed
chr21 products and index checkpoints. A stale failed benchmark is not a reusable
memory-profile baseline.

Start with a synthetic path-rich fixture at **2 million and 8 million steps**,
then a separately frozen real chr21 fixture targeting **at most 20 million steps**
and at most 100,000 complete named paths. Freeze complete Parents and all their
features, including retention pads, not partial GFF rows. Build its guide before
stripping pad paths. Control/candidate must consume the identical fixture and
guide. This profiling subset is not a new production annotation policy.

Synthetic diagnostic bounds: T=1 then T=4, 8 GiB cgroup cap, 5 GiB scratch,
10-minute command deadline. Real diagnostic bounds: T=1 then matched T=24,
32 GiB cap, 50 GiB scratch, 20-minute command deadline. Stop on a bound and retain
logs; do not silently increase it. Run one arm at a time. These are proposed
experimental ceilings, not measured runtime estimates.

Instrument prune boundaries: graph loaded; XG paths built; occurrence file
written; mapped sort complete; reverse arrays allocated/filled; file unmapped;
arrays compressed; mutable paths destroyed; pruning complete; guide loaded;
complement split; each unfold batch before/after generation/replay; graph extension.
Instrument RNA boundaries inside construction/augmentation and before/after
embedding batches, not only the current top-level cumulative maxima.

Record every 250 ms: RSS/PSS, anonymous/file/dirty memory, cgroup current/peak,
memory events, CPU and I/O counters. At phase boundaries record jemalloc stats
and sampled allocation stacks. `PackedGraph::report_memory(false)` already
reports graph/sequence, membership, links, steps and path-index categories;
use it sparingly because it scans paths and temporarily builds/sorts names.
Also record completed/edited step **size and capacity**, current haplotype
extraction bytes, component/log/mapping bytes and XG array widths. Separate
instrumented profiling from matched timing runs because profiling has overhead.

### B. Implement only width sizing first

Ownership is confined to `deps/xg/src/xg.cpp` plus focused XG tests and diagnostic
receipts. Derive widths during the existing record traversal. Leave mmmulti,
prune's protection logic, component scheduling and RNA unchanged for this A/B.
Rebuild the dependent XG archive and vg through the wrapper, verify the linked
binary hash, and freeze the candidate. Gate synthetic tests before a real slice.

Acceptance:

- Compare XG serialization and queries for empty graphs/paths, zero-membership
  nodes, repeated/reverse visits, circular paths, sparse IDs and bit-width
  boundaries. Preserve sorted membership order and 64-bit counts.
- For prune, both arms use `-u -a -k 32 -M 0`, identical guide, identical thread
  count and a **fresh separate seed mapping**. The closed `mapping.throwaway`
  already contains output mappings; do not use it as the fresh seed. The original
  harness initializes the seed as two uint64 values `(1000000000,1000000000)`.
- Require byte identity of **pruned graph and final mapping**, graph validity,
  unchanged guide and path inventories, unchanged retained sequence, and zero
  protected-path verification failures. `prune -v` warnings alone do not fail
  the process: explicitly fail the harness on any verification-failure report.
- Run relevant `t/38_vg_prune.t` coverage and XG tests; run a small mapped GCSA
  equivalence/query smoke after graph/mapping equality, without touching a
  production GCSA workspace.
- Check the predicted reverse-array byte reduction and which phase now owns
  RSS. Report total RSS, phase anonymous/file RSS, allocator live/retained bytes,
  wall time and filesystem I/O. No forced minimum RSS gain is a correctness gate;
  a unchanged peak means another owner dominates and determines the next change.
- The later RNA consumption experiment must use a separately changed candidate:
  T=1 graph/path/info bytes; at T>1 named transcript sequences, oriented named
  path walks and graph topology under a node-ID correspondence. Counts and node
  sequence multisets alone do not prove topology. Include retention paths and
  output-option combinations so earlier release cannot drop a later consumer.

### C. Resource and phase boundary

At inspection, the shared `user.slice` cap was 957.04 GiB and its nonreclaimable
charge was about 507.41 GiB, leaving about 449.6 GiB under the driver's admission
formula. SSD free space was approximately 10.4 TiB. These are a snapshot, not a
reservation. The driver excludes reclaimable cache from its charge, reserves
64 GiB host headroom, and requires a 2 TiB disk floor; use those rules and recheck
before each arm. Existing unrelated mapping/analysis jobs remain untouched.

No RNA/prune production baseline was rerun, no source optimization was applied,
and no profiling/benchmark phase was launched here. Work performed was bounded
metadata inspection and validation of the new inventory probe. Offer `/reload`
before the next substantive scientific phase, per the supplied project instructions.
