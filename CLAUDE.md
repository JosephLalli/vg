# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

A fork of `vgteam/vg` (variation-graph toolkit) at release **v1.75.1**, carrying
five fork-specific bodies of work plus a handful of small upstream patches
needed to compile here. Everything else is stock upstream `vg` and behaves as
documented in `README.md`.

Longer agent notes live in **[AGENTS.md](AGENTS.md)**. The five fork
subsystems each have a reference doc; read the relevant one before touching that
area:

| Area | Reference |
|---|---|
| Building on this machine | [BUILDING-LOCAL.md](BUILDING-LOCAL.md) |
| External-memory GCSA2 construction | [deps/gcsa2/EXTERNAL_MEMORY_CONSTRUCTION.md](deps/gcsa2/EXTERNAL_MEMORY_CONSTRUCTION.md) |
| `vg mpmap --trace-splice-search` | [MPMAP-SPLICE-TRACE.md](MPMAP-SPLICE-TRACE.md) |
| PhaseUnfolder/prune concurrency (absolute cost measured 2026-09-12; no controlled A/B) | this file, "Fork-specific subsystems" below |
| `vg rna` transcript-path memory | [docs/vg_rna_memory/README.md](docs/vg_rna_memory/README.md) |
| Transcript-rich RNA/prune memory follow-up (active validation) | [docs/transcript_path_memory/IMPLEMENTATION.md](docs/transcript_path_memory/IMPLEMENTATION.md) |
| GBWT creation performance (new, isolated attribution) | [docs/gbwt_creation/README.md](docs/gbwt_creation/README.md) |
| Prune phase profile and unfold scheduling (chr2, 2026-09-18) | [docs/prune_scheduling/README.md](docs/prune_scheduling/README.md) |

`docs/` holds design and status notes for the splice-discovery investigation
(mpmap vs. STAR splice junctions); they are working plans, not user docs.

## Current-work routing and scientific phase transitions

**This supersedes the former instruction to offer `/reload` when entering a new
scientific phase.** Do not run `/reload` automatically or require approval.
Use the documentation agent instead: reconcile current tracked state, retained
receipts, and live processes, then update AI-facing documentation with the
current intellectual state, code, decisions, evidence paths, and remaining
acceptance work.

The current exact-only chr21 RNA/prune rework is routed through
[docs/transcript_path_memory/IMPLEMENTATION.md](docs/transcript_path_memory/IMPLEMENTATION.md);
its generated artifacts are under `tmp/transcript_memory_20260915/`. It is
code-performance work, distinct from whole-genome production indexing and from
annotation/paralog policy work. Parallel V3 full chr21 RNA is terminal accepted
at 18.538162 GiB (1.30615977x OR), 33:31.11 wall and swap 0. The raw `.pg`
differs from the serial graph, so
`rna_parallel_output_v3/chr21-terminal/acceptance.json` accepted it through the
canonical semantic route: `vg validate`, all six graph/path fields (2,056,621
nodes, 2,726,485 edges and 5,607,688 named paths), exact info bytes and evidence
guards all pass. The preserved serial V3, 18.5631 GiB/1.3079x OR and 54:11.56,
remains the previous accepted baseline.

The GBWT-creation phase is routed through
[docs/gbwt_creation/README.md](docs/gbwt_creation/README.md). The optimized
guide and its coverage check passed; strip, genic_validate, genic_stats and
genic_paths then passed (14 sealed receipts). On 2026-09-18 the user
transferred ownership of the chr2 workflow ("it's your project now. alter the
sequence") and the six XG/distance stages were removed from the pre-prune plan
by the prune-direct transition
(`index_preprune_v2_20260917T044500Z/transitions/prune_direct_20260918T045514Z/`):
`vg prune -u` builds its own XG in-process and does not accept a prebuilt one,
so the plan is now `mapping` -> `READY_FOR_PRUNE` -> ordinary prune and checks
in the successor follower, then stop. The follower is now
`prune_v4_cap480_20260918T055521Z/` (user decision: prune cap 480 GiB, above
the 350-450 GiB planning band, admitted 2026-09-18 08:24 UTC; v3 at 512 GiB
was never admissible while the user's Docker caps held). No
`vg index -x` RSS measurement exists for the pinned binary at any scale; the
chr19 8.07x figure came from vg v1.74.1's mmmulti-based XG and does not
transfer. **The chr2 prune is terminal and the authorized work is finished.** The prune
stage exited 0 in 12:47:16 at 341.07 GiB peak RSS (71% of its 480 GiB cap), zero
swap, 2.006 effective cores of 24; `prune_check` passed (`graph: valid`,
50,647,839 nodes / 53,787,207 edges, mapping structurally verified) and
`PRUNE_COMPLETE.json` records `gcsa_executed: false`. All chr2 units have exited
and `.build-freeze` has lifted itself, so `./build-local.sh` works again. The
per-phase profile and the scheduling work it justifies are in
[docs/prune_scheduling/README.md](docs/prune_scheduling/README.md). This
authorizes no GCSA, annotation-policy work, or chr2 speedup claim.

V1 of the bounded parallel implementation passed its guarded build: PackedGraph
packing, info formatting, transcribed-node collection and missing-splice-edge
discovery followed by ordered graph mutation. Receipt:
`rna_parallel_output_v1/build/acceptance.json`; binary SHA256
`b0fa7af0cdb9864482bcd3eb764c4de7bc7a515d674c3c4cc38f396ab6192b38`.
Its terminal focused gate passed 48,455 assertions in 49 cases and real
1/2/4/24-thread semantic/info checks. The exact-byte dense ABBA averaged
28.1969 s serial and 20.0230 s at 24 threads, but the parallel runs averaged
only 1.326 CPU-seconds/wall-second. The useful-parallelism gate therefore
failed and no full parallel RNA run launched. V2 local-path microbatches and a
bounded persistent output ring passed 42,497 assertions in nine standalone
cases and retained exact dense bytes. Regular-file timing is inconclusive: the
writer was sampled in `balance_dirty_pages` under
`vm.dirty_bytes=100000000` while prune used the same SSD. A `/dev/null` ABBA
then isolated packing: V2 T24 averaged 1.9822 s versus 7.2406 s for V1 T24 and
20.2969 s for V2 T1; V2 local paths used about 18 effective cores. Receipt:
`rna_parallel_output_v2/discard/acceptance.json`. The terminal 500,039,680-step
discard scale then measured local paths at 6.294 wall/120.222 CPU seconds but
membership offsets at 29.274/35.926 and next links at 31.666/44.002. This
justifies the V3 source change to aligned membership microblocks and bounded
stable-bucket next-link waves. `rna_parallel_output_v3/BUILD_READY.json` pins
seven source hashes. Header-level validation is terminal: standalone passed
42,512 assertions in 11 cases and exact 50M bytes; the 500M packing-only ABBA
averaged 70.7286 s for V2 and 20.7334 s for V3, while the 2,056,621-node T1/T24
outputs were byte-identical. Parallel RSS was 1.7881 GiB for the large-node
50M case and at most 2.6983 GiB for 500M; the plan stayed within 3 GiB.
Receipts are `rna_parallel_output_v3/{standalone,post-header}/acceptance.json`.
The guarded build is terminal PASS with binary SHA256
`545de451f54b8bd291e4196e925dec4780f898bcb2d4bf96973b75d36b805f53`.
The initial integrated gate retained a cross-run T1 byte failure after 48,480
assertions in 52 passing unit cases, while info, validation and canonical
semantics matched. A same-binary repeat differed from that first T1 output and
matched the older gold; ID-preserving GFA for all three graphs was exact. This
localizes the difference to packed storage encoding/history but does not isolate
its precise cause. T1 raw-byte reproducibility is therefore not guaranteed;
same-input synthetic writer byte identity remains mandatory. Completion checks
are terminal PASS with stable guards and semantic/info/validation identity at
1/2/4/24 threads. The full parallel chr21 command is terminal success for
invocation `769f682f1e2f4b0db7a33daad48d230e`: exit 0, 33:31.11 wall,
19,438,672 KiB = 18.538162 GiB = 1.30615977x OR peak RSS, swap 0, exact info,
unchanged inputs, and a 38,126,287,944-byte streamed graph. Receipt:
`rna_parallel_output_v3/chr21/command-acceptance.json`. The independent
64 GiB/no-swap validator, invocation
`a64d62b5b6f04d0690d99bf3108f706f`, is terminal success; its semantic fallback
passes `vg validate`, all six canonical fields, exact info, and stable evidence
guards. `chr21/phase-analysis.json` remains immutable command evidence binding
the unchanged 574-file source manifest; its historical pending wording is
superseded by the terminal receipt. The 38.1494% lower wall than the accepted serial V3 is an
unmatched, nonexclusive-host comparison, not a controlled causal speedup. The
remaining seam audit is `rna_parallel_output_v3/serial-followup.md`. Treat the
accepted result as the workable baseline. The isolated metadata pilot is
terminal PASS (invocation `9c6d2ba45e3c40cb8c4b6aaac7ae94ad`) with stable
guards. Its attribution led to a private two-check removal whose gate is also
terminal PASS (invocation `52f675bae8284b1fb8051468f5216ff8`):
`rna_metadata_lookup_v1/checks/units/stdout` reports 42,622 assertions in 14
cases, while `rna_metadata_lookup_v1/checks/acceptance.json` binds exact retained
100,000-name bytes at T1/T24 and a
5,607,688-name CPU-only ABBA reducing mean whole-writer wall from 93.4301 to
77.5504 s (17.0%), metadata from 87.3692 to 71.2887 s, and mean writer CPU from
128.2705 to 113.113 s without increased peak RSS. The private prototype is now integrated into the live source tree after the
current-binary prune proof gate closed. The integrated patch changes exactly
`deps/libbdsg/bdsg/include/bdsg/internal/base_packed_graph.hpp` and
`src/unittest/packed_path_stream.cpp`; `rna_metadata_lookup_v1/integration/apply/acceptance.json`
records the patch SHA256, the two changed paths, 572 unchanged production
sources and stable live-library guards. The guarded build service
`vg-memory-rna-metadata-integration-build-v1-20260916.service` (invocation
`1a9c4cf6b67f4410899b6558ad8a49b2`) is terminal PASS with binary SHA256
`4f495d705c5547a39d1334a9c6cd7d4e02ece9e50fe65831ea79ba2679b4273c`. The
guarded check service `vg-memory-rna-metadata-integration-checks-v1-20260916.service`
(invocation `3d32e35743a845c992afa34c8639e333`) is terminal PASS: linked
metadata tests, unit success, stable sources/inputs/library, and T1/T24 real
fixtures pass validation, exact info and canonical fingerprints. T1 preserves
exact graph bytes; T24 differs only in raw packed-history bytes. This closes
integration correctness for the small metadata optimization, not a full-RNA
runtime claim. A whole-RNA rerun is unnecessary absent a new integration
concern. Defer shared-source translation and compaction subregion work. See
`rna_parallel_output_v2/README.md` for the scale and writeback-cap controls.
Prune V5 then terminated at its exact eight-hour service deadline with
`Result=timeout`, status 15 and a 62 GiB peak. It has no terminal measured/GNU
receipt, output graph, or semantic check, so it is rejected as a performance or
memory result. `chr21_prune/candidate-v5-terminal-timeout.json` is authoritative.
Ordinary V2 remains accepted at 75.608 GiB/1.80365x OR and 4:14:01; the 1.5x
prune target remains unmet. V2 is binary-specific, so the frozen current binary
is undergoing one ordinary-default revalidation in `prune_current_binary_v1/`.
Its fixture service invocation `dece0c5939e34d8d8eb19124001a0e9c` is terminal
PASS: TAP 26 and ordinary T1/T4/T24 plus real T24 have exact graph/mapping bytes
and validation with stable guards. The full current-binary command invocation
`be246266875c49e48228747eebedad77` is terminal PASS at 74.815071 GiB
(1.784731532x OR), 3:21:53 wall, zero swap, exact mapping bytes and accepted
graph semantics. The original terminal checker failed closed only because the
current raw graph hash was unfamiliar; its pair-specific packed-history proof
is retained under `prune_current_binary_v1/chr21-terminal/storage-classification-v1/`.
The proof-backed terminal checker
`vg-memory-prune-current-terminal-proof-v1-20260916.service` (invocation
`d87bccbdf53a428a99515f1afcf366d6`) is terminal PASS at
`prune_current_binary_v1/chr21-terminal-current-proof/acceptance.json`. This
closes delivery provenance for the required `<2x` prune goal and does not meet
or authorize a speculative 1.5x run. The ledger names the guarded build/check
launchers and source hashes. Read it and verify live resources before acting;
no production-index or annotation conclusion follows from these code benchmarks.

## Building — never a bare `make`

Homebrew on this machine has drifted (protobuf 33.4, shared-only abseil, and an
`sdsl`/`divsufsort` that shadow vg's vendored copies), so a plain `make` fails.
`./build-local.sh` is a thin wrapper that injects the local static
protobuf-v29.3 toolchain and the right compilers/flags, then calls `make`, so
incremental builds, single targets, and `clean` all work through it.

```bash
./build-toolchain.sh              # ONCE: local static protobuf+abseil (~2 min)
./build-local.sh                  # build bin/vg; incremental after the first run
./build-local.sh obj/foo.o        # compile one object, skip the ~1-2 min relink
./build-local.sh bin/vg           # relink when ready
./build-local.sh clean            # make clean, then full rebuild (~20-40 min)
JOBS=24 ./build-local.sh          # -j value; default 64
```

Any arguments are forwarded to `make` as targets. New `src/*.cpp` files are
picked up automatically (`Makefile:309` globs the directory); editing a header
recompiles every object that includes it, tracked via `obj/**/*.d`.

**Editor/LSP diagnostics are not authoritative** — clangd does not see the
vendored include paths or toolchain headers the wrapper injects. Only a real
`./build-local.sh` compile decides whether something builds.

Why each setting exists, and the fork's upstream patches (a `libbdsg` `-j` race
fix, C++17 for vendored sparsehash, `vg::identity(...)` qualifications for
C++20), are tabulated in BUILDING-LOCAL.md.

## Tests

Integration tests are bash-tap scripts under `test/t/`, run with `prove` from
the `test/` directory. Unit tests are Catch2, compiled into the binary and run
via the `vg test` subcommand.

```bash
cd test && prove -v t                     # whole integration suite
cd test && prove -v t/33_vg_mpmap.t       # one test file
./bin/vg test                             # all unit tests
./bin/vg test "[mapping]"                 # Catch2 tag filter
./bin/vg test --list-tests                # enumerate
make lint                                 # scripts/check_options.py
```

`make test` runs `lint` first, then the `prove` suite, then `doc/test-docs.sh`
with the compiler environment scrubbed. Any new `src/subcommand/*_main.cpp` must
satisfy `scripts/check_options.py` (help-text and option-formatting rules) or
the lint gate fails.

Tests relevant to the fork work:

- `t/58_vg_gcsa_external.t` — external GCSA2 equivalence and resume/recovery
- `t/06_vg_index.t`, `t/52_vg_autoindex.t` — index regressions
- `t/33_vg_mpmap.t`, `t/35_vg_mpmap_trace.t` — mpmap and the splice trace

Test 33 needs the BSD `rs` utility (installed at `~/usr/local/bin/rs`); if it is
missing, subtests 12/13 fail on `command not found`, not on a vg change.

## Architecture

**Graph representation is an interface, not a class.** `deps/libhandlegraph`
defines `handle_t`/`nid_t` and the `HandleGraph` hierarchy; every graph
implementation (`src/vg.hpp`'s `VG`, the `libbdsg` packed/HashGraph types, `xg`,
`gbwtgraph`) implements some subset. `src/handle.hpp` re-exports the handlegraph
namespace into `vg::`. Code should take the weakest handle-graph interface it
needs rather than a concrete type. Serialized graph formats are read and written
through `deps/libvgio` (protobuf + blocked-gzip), registered at startup by
`src/io/register_libvg_io.hpp`.

**Subcommands self-register.** `src/subcommand/subcommand.hpp` defines a
compile-time registry: each `*_main.cpp` constructs a static `Subcommand` global
with a name, description, category, and entry function. `main.cpp` includes no
subcommand headers. The consequence to remember: those objects must be linked
into the binary explicitly — nothing references their symbols, so they will not
be pulled out of an archive.

**Indexes are declared products with recipes.** `src/index_registry.{hpp,cpp}`
names each index (`IndexName`) and registers recipes that produce it from other
indexes. `vg autoindex` plans a path through that graph of recipes rather than
hard-coding a pipeline, which is why adding an index type means adding registry
entries, not a subcommand.

**Three mapper families, three different seeding strategies**, sharing the
aligners in `src/aligner.hpp` / `deps/gssw` / `deps/dozeu`:

- `src/mapper.{hpp,cpp}` — the original GCSA2 MEM-based `vg map`
- `src/multipath_mapper.{hpp,cpp}` — `vg mpmap`, MEM seeding into multipath
  alignments; the splice/RNA path lives here
- `src/minimizer_mapper*.{hpp,cpp}` — `vg giraffe`, minimizer + GBWT haplotype
  seeding and chaining

`src/algorithms/` (88 files) holds graph algorithms kept independent of any
particular mapper. Dependencies are 44 git submodules under `deps/`; several are
vgteam projects developed in lockstep with vg.

## Fork-specific subsystems

### External-memory GCSA2 construction

`deps/gcsa2` is a fork of GCSA2 that adds a durable-workspace, external-memory
construction path (bounded memory and disk budgets, framed and compressed spill
streams, resumable checkpoints) so a whole-pangenome index can be built without
holding the prefix-doubling state in RAM. The design, resource invariants,
commit/recovery protocol, and implementation ledger are in
`deps/gcsa2/EXTERNAL_MEMORY_CONSTRUCTION.md`; read it before changing spill,
join, or checkpoint code.

On the vg side this surfaces as `src/gcsa_workspace.{hpp,cpp}` plus `--gcsa-*`
flags on `vg index` and `vg autoindex` (work dir, resume, memory/disk limits,
sort-run and join-partition sizes, temp compression, process workers). A named
source graph stays **one semantic GCSA2 input** even when later phases spill to
many physical files — do not treat spill shards as logical inputs.

```bash
JOBS=1 ./build-local.sh -C deps/gcsa2 test     # tiny byte budgets force multi-run paths
JOBS=8 ./build-local.sh -C deps/gcsa2 all
cd test && prove -v t/58_vg_gcsa_external.t
```

The vg `Makefile` tracks `deps/gcsa2/src/*.cpp` and `include/gcsa/*.h` as
prerequisites of `lib/libgcsa2.a` (`Makefile:654`); that
dependency correction is a fork patch. Without it a source-only GCSA2 change can
relink a `vg` that still uses a stale `lib/libgcsa2.a`.

For end-to-end memory/IO ceilings use `scripts/benchmark-gcsa-external.sh`,
which pins input and executable hashes, records `/usr/bin/time -v`, and emits
`phase_summary.tsv` and `summary.tsv`.

### mpmap splice-search instrumentation

`vg mpmap --trace-splice-search FILE` (optionally `--trace-truth-junctions
FILE`) is an observational, disabled-by-default JSONL trace of the MEM
seeding / clustering / soft-clip-gated splice-rescue search space per read. It
changes no mapping behavior and adds no aligner; it exists to decide empirically
whether graph MEMs suffice for STAR-like novel splice-junction discovery. Code:
`src/mpmap_trace.{hpp,cpp}` with hooks in `src/multipath_mapper.cpp` and
`src/subcommand/mpmap_main.cpp`. Schema, field reference, and the limits of what
it can answer are in MPMAP-SPLICE-TRACE.md.

```bash
cd test && prove -v t/35_vg_mpmap_trace.t
```

### PhaseUnfolder/prune concurrency (absolute cost measured; no controlled A/B)

Three merged commits ancestral to HEAD attack `vg prune -u`'s near-single-threaded
wall-clock cost, which is the largest per-chromosome cost in the whole-genome
transcript-pangenome pipeline (15.6-59h per chromosome measured, CPU% consistently
100-145% regardless of `-t`):

- `bbf264574` "Unfold complement components concurrently"
  (`src/phase_unfolder.{cpp,hpp}`) — parallelizes the per-component complement-graph
  unfold, previously one component at a time (53,911 components on chr2's genic graph
  took 1h43m of a 4h18m prune, 40% of the run, on a single core with 47 of the 48
  requested threads asleep).
- `409c30a77` "Delete the pruned graph's paths in one call"
  (`src/subcommand/prune_main.cpp` only — it does *not* touch `phase_unfolder.cpp`)
  — replaces a one-`destroy_path()`-at-a-time path deletion that dominated a joint
  chr20+21+22 prune (7,081s of 10,222s total).
- `b47de4db9` "prune: batch PackedGraph edge deletion" (`src/algorithms/prune.cpp`,
  `test/t/38_vg_prune.t`, plus a `deps/libbdsg` gitlink bump) — batches the per-node
  edge deletion the prune pass issues.

**Each commit records a byte-identity gate; what the gates don't establish is the
speedup.** `bbf264574` gated on byte-identity of the pruned graph *and* the node
mapping, not the graph alone; `409c30a77` records byte-identical results in `-e`, `-r`
and `-u` modes, `-u` being the mode the pangenome pipeline uses, and `b47de4db9`
carries `t/38_vg_prune.t` coverage. Do not repeat the earlier claim in this file that
nothing confirms their safety — that was wrong.

A **controlled A/B** is still missing: the one built to measure the speedup
(`prune_benchmarks/chr21_unfold_ab` in the downstream `hprc_v2_vg_rna` workspace)
aborted 5.5 minutes into its serial control arm and never ran the parallel arm. That
abort is understood — the user session had `Linger=no`, so `systemd-run --user --scope`
died on session teardown (exit 143); `loginctl enable-linger` is set now — but the A/B
itself has not been rerun. Do not attribute a specific multiple to these three commits
until it is.

A **per-phase profile** now exists for chr2 (2026-09-18):
[docs/prune_scheduling/README.md](docs/prune_scheduling/README.md). It ranks the
serial costs — XG construction 6.2 h (59%), `complement_components` 2.7 h (26%),
the already-parallel unfold third, the prune passes 0.1% — and records that the
unfold decays to 2 of 24 busy threads because the batch barrier at
`phase_unfolder.cpp:52` drains onto each batch's largest component. It proposes
LPT ordering and a bounded reorder buffer, both byte-identity-preserving, and
defers XG construction to its own phase. No change is authorized while the chr2
production prune is live.

What also exists (2026-09-12) is a **pangenome-scale absolute timing**, which is a
different and weaker claim: not a controlled A/B, but a measurement of the actual
binary (sha256 `35867c7f…`) that will run the whole-genome build.
`scripts/whole_genome/build_joint_genic_k32_index.sh calibrate` in the downstream
workspace ran `vg prune -p -u -k 32 -M 0 -t 96 -g <guide> -a -m <throwaway>` against a
throwaway node mapping and discarded the pruned output, on three stripped genic
chromosome graphs:

| chromosome | stripped genic graph | wall | peak RSS |
|---|---|---|---|
| chrY | 0.32 GB | 11:41.91 | 6.05 GiB |
| chr18 | 5.96 GB | 38:25.97 | 42.28 GiB |
| chr2 | 26.64 GB | 2:41:33 | 194.65 GiB |

Scaling from chr18 to chr2 is linear in graph size (4.47x the graph for 4.20x the wall,
4.60x the memory); chrY is a small-graph outlier and must not be used as a scaling
anchor — a two-point fit through it predicted chr2 at 70.5 min against an actual
161.55 min. Projected over the 23-chromosome, 321 GB genic corpus: **~33h serial**,
against a joint-index driver that had been planned around roughly 22 days. chr2's CPU
profile (304 samples at 30s intervals) averaged 518%, peaked at 791%, with 84% of
samples above 200% — the first pangenome-scale evidence that `bbf264574` actually
engages; the pre-optimization signature recorded above is "CPU% consistently 100-145%
regardless of `-t`."

A before/after on chr2's genic scope against `bbf264574`'s own commit note (4h18m, of
which 1h43m was the single-threaded unfold) is suggestive — Amdahl on the unfold alone
predicts 2h36m, measured 2h41m, within 3% — but it is **not** a controlled A/B
(different binary; the annotation and graph were rebuilt in between) and must not be
read as attributing a clean multiple to these three commits versus anything else that
changed in between; it does not replace `chr21_unfold_ab`. Full write-up:
`hprc_v2_vg_rna/notes/prune_calibration_2026-09-12.md`.

Memory, not time, is now the binding constraint on prune concurrency at pangenome
scale: chr2 peaked at 194.65 GiB against a 280 GiB cap (70%); two large chromosomes
pruning concurrently would want ~390 GiB. Keep prune serial for large chromosomes, or
pair a large one with a small one.

**Transcript-rich inputs add a separate memory cost.** The retained chr21 OR versus
exact-only comparison (2026-09-14, in `hprc_v2_vg_rna`:
`notes/chr21_exact_only_downstream_ab.md`) measured prune peak RSS at 41.9 versus
256.7 GiB while retained representatives grew 5.35x and biological path steps grew
7.865x. Prune used 48 versus 24 threads, and GCSA used 32 versus 24; their timing
ratios are not controlled selection effects. Both arms completed, but the retained
prune logs do not identify the peak-owning phase. The current allocation analysis
identifies overlapping input PackedGraph paths, XG paths, and XG reverse-index
construction as a leading candidate; it does not assign all 256.7 GiB to unfolding.
See [the attribution report](docs/transcript_path_memory/README.md) and
[active implementation/validation](docs/transcript_path_memory/IMPLEMENTATION.md).
That work preserves the exact-only workload and is separate from v2 production
indexing and paralog-aware annotation correction.

Note `b47de4db9` moves the `deps/libbdsg` gitlink to a fork commit
(`b07563bb9`, branch `packed-graph-batch-edge-deletion` on `JosephLalli/libbdsg`)
while `.gitmodules` still names `vgteam/libbdsg`, which does not carry that commit.
A fresh clone therefore cannot resolve the submodule until that URL is repointed.

### `vg rna` transcript-path memory

Four merged commits cut `vg rna`'s peak RSS on the transcript route by roughly 6x,
gated on byte-identical output. The whole-genome transcript-pangenome pipeline runs
`vg rna` per chromosome, and its peak was what made the large chromosomes
infeasible. All four touch only `src/transcriptome.{cpp,hpp}` (net +307/-175):

- `4cd1c5dd3` — `EditedTranscriptPath::path` becomes a `vector<EditedMapping>`, a
  16-byte `{handle, offset, length}` record, replacing a protobuf `Mapping` of roughly
  190 bytes. This was 46.5% of the chrY heap profile.
- `583bfd90c` — releases `translations` and `exon_boundary_paths` as soon as they are
  consumed, and drains the edited list as it goes. `make_translation`'s 2N
  `Translation` objects were another 45.9%.
- `83c36ea51`, `28dc0b7be` — divide nodes at exon boundaries directly instead of
  calling `vg::augment`, **scoped to the transcript route**.

**The bypass is transcript-route only.** `augment()` is still called when
`is_introns` is true, because augment's second pass creates intron splice-junction
edges the first pass does not reproduce — an unscoped bypass failed
`vg test [transcriptome]` at `transcriptome.cpp:842` with `get_edge_count() == 15`
against an expected 17. So `vg rna -m` (intron BED input) keeps the old memory
profile; do not quote these numbers for it.

Measured on chr20, against a control-vs-control noise floor of 0.04%:

| run | peak RSS | wall |
|---|---|---|
| control `-t 1` | 105.32 GiB | 22:17 |
| patched `-t 1` | 15.31 GiB | 15:07 |
| control `-t 64` | 105.22 GiB | 15:24 |
| patched `-t 64` | 17.00 GiB | 11:04 |

At `-t 1` the patched run is byte-identical to the control across the output graph,
`prune_paths.gbwt`, `info.tsv` and the transcript FASTA. `vg test [transcriptome]`
and `vg validate` pass.

Historical `-t 1` controls were byte-identical, but a later same-binary V3
diagnosis produced two raw PackedGraph encodings with exact ID-preserving GFA,
so **T1 byte reproducibility is not guaranteed**. Above one thread, thread completion
can set `exon_boundary_paths` order, which sets `unordered_map` insertion order in
`find_breakpoints`, which sets `divide_handle` order, which sets the output node IDs.
The graph is isomorphic and the transcripts identical, but the file is not
reproducible — so byte-identity gates must run at `-t 1`, while production runs
freely use more. Relabel-invariant digests (name-sorted FASTA, GBWT path
name/length/step-count, node-sequence multiset, `vg stats -N -E -l -z`) are what to
compare at `-t > 1`.

Full method, patches and per-run results are in
[docs/vg_rna_memory/README.md](docs/vg_rna_memory/README.md).

The 2026-09-15 transcript-rich follow-up reuses those merged improvements and
targets the remaining exact-only chr21 RNA/prune peaks. Its opt-in mapped graph
and path-spool work, byte-gate investigations, resource limits, and retained
receipts are tracked in
[docs/transcript_path_memory/IMPLEMENTATION.md](docs/transcript_path_memory/IMPLEMENTATION.md).
V3 full RNA now has terminal full-workload semantic and memory acceptance. V1
parallel-source checks passed correctness but failed the useful-CPU gate; V2
local scheduling has standalone correctness and packing-only parallelism. Its
500M-step scale exposed coarse offset/next scheduling. V3 header-level
correctness, scaling and memory gates and its guarded build now pass. The first
integrated gate exposed semantically neutral T1 packed-byte variation; completion
checks pass and the full chr21 measurement is live, with no end-to-end parallel
result yet. Prune V5 timed out without output or semantic acceptance; ordinary
V2 remains the accepted prune result. Fixture measurements are not
production-indexing results.

## Conventions

- Keep divergence from upstream v1.75.1 minimal and deliberate; the fork patches
  listed in BUILDING-LOCAL.md are the complete inventory of non-feature changes.
- Temp-heavy work (GCSA2 construction, `vg index`, sorts) must set `TMPDIR` to an
  SSD path under the checkout or job dir — the system default is on a slow HDD.
