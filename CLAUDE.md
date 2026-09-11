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
| PhaseUnfolder/prune concurrency (timing unvalidated) | this file, "Fork-specific subsystems" below |
| `vg rna` transcript-path memory | [docs/vg_rna_memory/README.md](docs/vg_rna_memory/README.md) |

`docs/` holds design and status notes for the splice-discovery investigation
(mpmap vs. STAR splice junctions); they are working plans, not user docs.

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

### PhaseUnfolder/prune concurrency (timing unvalidated)

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

**Each commit records a byte-identity gate; what no measurement establishes is the
speedup.** `bbf264574` gated on byte-identity of the pruned graph *and* the node
mapping, not the graph alone; `409c30a77` records byte-identical results in `-e`, `-r`
and `-u` modes, `-u` being the mode the pangenome pipeline uses, and `b47de4db9`
carries `t/38_vg_prune.t` coverage. Do not repeat the earlier claim in this file that
nothing confirms their safety — that was wrong. What is genuinely missing is a
**pangenome-scale timing**: the one A/B built to measure it
(`prune_benchmarks/chr21_unfold_ab` in the downstream `hprc_v2_vg_rna` workspace)
aborted 5.5 minutes into its serial control arm and never ran the parallel arm, so no
speedup number exists. That abort is now understood — the user session had
`Linger=no`, so `systemd-run --user --scope` died on session teardown (exit 143);
`loginctl enable-linger` is set now. Re-run that A/B to completion before quoting a
speedup for a production build.

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

**`vg rna` is byte-reproducible only at `-t 1`.** Above one thread, thread completion
order sets `exon_boundary_paths` order, which sets `unordered_map` insertion order in
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
