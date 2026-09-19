# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

A fork of `vgteam/vg` (variation-graph toolkit) at release **v1.75.1**, carrying
**five fork-specific subsystems** plus a handful of small upstream patches needed
to compile here. Everything else is stock upstream `vg` and behaves as documented
in `README.md`.

Read the reference for an area before touching it. Status is as of 2026-09-19.

| Area | Status | Reference |
|---|---|---|
| Building on this machine | — | [BUILDING-LOCAL.md](BUILDING-LOCAL.md) |
| External-memory GCSA2 construction | open | [deps/gcsa2/EXTERNAL_MEMORY_CONSTRUCTION.md](deps/gcsa2/EXTERNAL_MEMORY_CONSTRUCTION.md) |
| `vg mpmap --trace-splice-search` | observational, disabled by default | [MPMAP-SPLICE-TRACE.md](MPMAP-SPLICE-TRACE.md) |
| PhaseUnfolder/prune concurrency | merged; **no controlled A/B** | below, and [docs/prune_scheduling/README.md](docs/prune_scheduling/README.md) |
| `vg rna` transcript-path memory | merged, byte-identity gated | [docs/vg_rna_memory/README.md](docs/vg_rna_memory/README.md) |

Two further bodies of work have reference docs but are not upstream-divergent
subsystems — they are measurement and production records:

| Record | Status | Reference |
|---|---|---|
| Transcript-rich chr21 RNA/prune | terminal accepted | [docs/transcript_path_memory/IMPLEMENTATION.md](docs/transcript_path_memory/IMPLEMENTATION.md) |
| GBWT creation and the chr2 index run | terminal | [docs/gbwt_creation/README.md](docs/gbwt_creation/README.md) |
| chr2 prune phase profile | terminal (2026-09-18) | [docs/prune_scheduling/README.md](docs/prune_scheduling/README.md) |

`docs/` holds two distinct kinds of file. The four subdirectories above
(`gbwt_creation/`, `prune_scheduling/`, `transcript_path_memory/`,
`vg_rna_memory/`) are authoritative references for their subsystem. The loose
`docs/*.md` files are working plans for the splice-discovery investigation
(mpmap vs. STAR splice junctions) and are neither user docs nor settled results.

## Current state

**This supersedes the former instruction to offer `/reload` when entering a new
scientific phase.** Do not run `/reload` automatically or require approval.
Use the documentation agent instead: reconcile current tracked state, retained
receipts, and live processes, then update AI-facing documentation with the
current intellectual state, code, decisions, evidence paths, and remaining
acceptance work.

**Keep three efforts distinct: vg code performance, production indexing, and
annotation/paralog policy.** A result in one authorizes nothing in the others.
This is the most reused rule in this file.

The pinned production binary is SHA256
`4f495d705c5547a39d1334a9c6cd7d4e02ece9e50fe65831ea79ba2679b4273c`.

**chr2 production index — prune is terminal; GCSA2 is not started.**
`vg prune -p -u -k 32 -M 0 -t 24` exited 0 in 12:47:16 at 341.07 GiB GNU-time
peak RSS under a 480 GiB cap, zero swap, 2.006 effective cores of 24.
`prune_check` passed and `PRUNE_COMPLETE.json` records `gcsa_executed: false`.
Outputs are `chr2.pruned.pg` (6,774,195,898 bytes; 50,647,839 nodes,
53,787,207 edges) and `chr2.mapping` (340,858,176 bytes). The six XG/distance
stages were removed from the pre-prune plan because `vg prune -u` builds its own
XG in-process; XG and distance are still required for `vg mpmap` and remain an
open design question. No other chromosome in this generation has been pruned.
This authorizes no GCSA2, no annotation-policy change, and no chr2 speedup
claim. → [docs/gbwt_creation/README.md](docs/gbwt_creation/README.md),
[docs/prune_scheduling/README.md](docs/prune_scheduling/README.md)

**Transcript-rich chr21 RNA/prune — terminal accepted.** Parallel V3 full chr21
RNA at 18.538162 GiB and 33:31.11; ordinary prune on the current binary at
74.815071 GiB and 3:21:53. The `<2x` prune goal is met; the speculative 1.5x
goal is unmet and not authorized. Raw `.pg` bytes are not reproducible above
`-t 1`, so acceptance runs through the canonical semantic route. Every receipt,
invocation ID, assertion count and ABBA timing lives in the ledger, not here.
→ [docs/transcript_path_memory/IMPLEMENTATION.md](docs/transcript_path_memory/IMPLEMENTATION.md)

**Whole-genome production indexing and annotation/paralog policy** are separate
efforts with their own acceptance paths in the downstream `hprc_v2_vg_rna`
workspace. Nothing in this file authorizes work in either.

Verify live resources before acting. No production-index or annotation
conclusion follows from the code benchmarks recorded here.

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

### The build freeze

`build-local.sh` refuses to build (exit 3) while a `.build-freeze` file exists
whose `until=` path does not. It exists because the `vg-pinned` copies used by a
production run resolve `lib/libhandlegraph.so` from this worktree by **RPATH**
(not RUNPATH, so `LD_LIBRARY_PATH` cannot redirect it), and the run pins that
file by hash *and* inode/mtime, re-checked after the stage command succeeds but
before its outputs are sealed. A relink mid-stage therefore discards a completed
multi-hour stage. Create `.build-freeze` with `reason=`, `since=` and `until=`
lines before a long pinned run; it lifts itself once the receipt at `until=`
exists. `VG_BUILD_UNFREEZE=1` overrides it deliberately. The file is
`.gitignore`d because its `until=` path is absolute and machine-specific —
committing it breaks `./build-local.sh` in every other checkout.

### Submodule hazard

`b47de4db9` moves the `deps/libbdsg` gitlink to a fork commit (`b07563bb9`,
branch `packed-graph-batch-edge-deletion` on `JosephLalli/libbdsg`) while
`.gitmodules` still names `vgteam/libbdsg`, which does not carry that commit.
A fresh clone cannot resolve the submodule until that URL is repointed.

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
wall-clock cost, the largest per-chromosome cost in the whole-genome
transcript-pangenome pipeline. The signature they targeted, measured *before*
these commits and quoted here as history rather than current cost, was 15.6-59 h
per chromosome at a CPU% consistently 100-145% regardless of `-t`. The current
measured cost is 12:47:16 for chr2 at 2.006 effective cores of 24; see the
per-phase profile below.

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

**The chr2 per-phase profile (2026-09-18)** is the first breakdown of a
pangenome-scale prune, and it reorders the targets:
[docs/prune_scheduling/README.md](docs/prune_scheduling/README.md). Of a
12:47:16 run, XG construction took 6.19 h (48%) on one core, the unfold
3.63 h (28%) averaging 4.51 of 24 cores, `complement_components` 2.70 h (21%)
single-threaded, graph load 1.4%, `extend` plus serialization 0.7%, and the
prune passes that `b47de4db9` optimised 0.1%. Overall the run used **2.006
effective cores of the 24 requested**, with roughly 8.9 of its 12.8 hours
strictly single-threaded. The unfold's CPU is U-shaped — a fast start, a ~100
minute trough at or below 3 cores bottoming at 1.0, then recovery to 6-7.7 — the
signature of the batch barrier at `phase_unfolder.cpp:52` draining onto each
batch's largest component. Its recoverable share is bounded at about 3 h of
12.8 (981.5 core-minutes ideally scheduled is 40.9 min against 217.7 actual).
The peak RSS of 341.07 GiB falls **in the unfold**, not in XG construction,
which releases its 336.89 GiB path plateau before the reverse index is built.
The doc proposes measuring the per-component distribution, then
longest-processing-time ordering and a bounded reorder buffer, both of which
preserve byte-identity because apply order rather than work order fixes the
numbering.

Read that profile alongside the CPU average below. The 2026-09-12 calibrate run
on a 26.64 GB stripped chr2 genic graph gave 2:41:33 at 194.65 GiB, and its CPU
profile averaged 518% with 84% of samples above 200% — real evidence that
`bbf264574` engages, but an average that hides the distribution the profile
measured. That calibration also projected ~33 h of serial prune over a
23-chromosome 321 GB corpus; production inputs in the current generation are
roughly 6x larger per chromosome (chr2's is 173.58 GB), so treat the projection
as describing an input class that no longer matches production. Full write-up:
`hprc_v2_vg_rna/notes/prune_calibration_2026-09-12.md`, which also records the
Amdahl before/after — predicted 2h36m against a measured 2h41m, within 3% — and
correctly refuses the causal reading, since the binary, annotation and graph all
changed in between.

**Memory, not time, binds prune concurrency at pangenome scale.** The production
chr2 prune peaked at 341.07 GiB against a 480 GiB cap, so two large chromosomes
pruning concurrently would want roughly 680 GiB of the host's 1,007 GiB. Keep
prune serial for large chromosomes, or pair a large one with a small one.

**Transcript-rich inputs add a separate memory cost**, measured by a controlled
chr21 A/B: retaining 5.35x the representatives moved prune peak RSS from
41.9 GiB (OR) to 256.7 GiB (exact-only), a 6.12x ratio, while stages that track
nodes rather than paths — the pruned graph, and GCSA2 — moved only 1.06-1.15x.
That A/B's extrapolation to chr2 predicted ~1,192 GiB and was falsified by the
measured 341.07 GiB; see the correction in
`hprc_v2_vg_rna/notes/chr21_or_vs_exact_dedup_downstream.md`. Attribution work:
[docs/transcript_path_memory/README.md](docs/transcript_path_memory/README.md).

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

## Conventions

- Keep divergence from upstream v1.75.1 minimal and deliberate; the fork patches
  listed in BUILDING-LOCAL.md are the complete inventory of non-feature changes.
- Temp-heavy work (GCSA2 construction, `vg index`, sorts) must set `TMPDIR` to an
  SSD path under the checkout or job dir — the system default is on a slow HDD.
- **Byte-identity gates run at `-t 1`.** Above one thread, node IDs depend on
  thread completion order, so compare relabel-invariant digests instead:
  name-sorted FASTA, GBWT path name/length/step-count, node-sequence multiset,
  and `vg stats -N -E -l -z`. Production runs may use any thread count.
- **Define a ratio's denominator on first use.** Ratios written `1.30x OR` in
  older notes are against the OR-dedup arm of the chr21 comparison; prefer
  absolute GiB with the baseline named.
- Do not attribute a speedup without a controlled A/B. An absolute timing, an
  uncontrolled before/after, and a projection are each weaker claims, and this
  file's history shows all three being read as the strong one.
