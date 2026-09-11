# Agent notes for this `vg` fork

This is a fork of `vgteam/vg` at release **v1.75.1** carrying three fork-specific
things, plus a smaller, unvalidated concurrency change to upstream prune code.
Read the linked docs before working on any of them.

## 1. Building — never a bare `make`

Homebrew on this machine has drifted (protobuf 33.4, shared-only abseil, shadowing
`sdsl`/`divsufsort`), so a plain `make` fails. **Always build with
`./build-local.sh`**, which injects the local static protobuf-v29.3 toolchain and
the right compilers/flags. It is a thin wrapper around `make`, so incremental
builds, single-object targets, and `make clean` all work through it.

See **[BUILDING-LOCAL.md](BUILDING-LOCAL.md)** for the full setup, the incremental
workflow, and the small upstream patches this fork carries (a `libbdsg` `-j` race
fix in the `Makefile`; `vg::identity(...)` qualifications in `inject_main.cpp`,
`readfilter.hpp`, `minimizer_mapper_from_chains.cpp` for C++20).

## 2. External-memory GCSA2 construction

`deps/gcsa2` is a fork of GCSA2 adding a durable-workspace, external-memory
construction path (bounded memory/disk budgets, framed compressed spill
streams, resumable checkpoints), so a whole-pangenome index can be built
without holding the prefix-doubling state in RAM. Full reference — design,
resource invariants, commit/recovery protocol, and implementation ledger — is
in **[deps/gcsa2/EXTERNAL_MEMORY_CONSTRUCTION.md](deps/gcsa2/EXTERNAL_MEMORY_CONSTRUCTION.md)**;
read it before changing spill, join, or checkpoint code.

On the `vg` side: `--gcsa-*` flags on `vg index`/`vg autoindex` (work dir,
resume, memory/disk limits, sort-run and join-partition sizes, temp
compression, process workers), backed by
`src/gcsa_workspace.{hpp,cpp}`. Test: `t/58_vg_gcsa_external.t`
(`cd test && prove -v t/58_vg_gcsa_external.t`).

## 3. The `vg mpmap` splice-search instrumentation

The fork adds `vg mpmap --trace-splice-search FILE` (with optional
`--trace-truth-junctions FILE`): an observational, disabled-by-default trace layer
that measures the MEM-based seeding / clustering / soft-clip-gated splice-rescue
search space per read, to decide empirically whether graph MEMs suffice for
STAR-like novel splice-junction discovery or whether a STAR-like seed/MMP front
end is warranted. It changes no mapping behavior and adds no new aligner.

Full reference — flags, JSONL schema, field meanings, where metrics are collected,
and what it can/cannot answer — is in
**[MPMAP-SPLICE-TRACE.md](MPMAP-SPLICE-TRACE.md)**.

Code: new `src/mpmap_trace.{hpp,cpp}`; hooks in `src/multipath_mapper.cpp` and
`src/subcommand/mpmap_main.cpp`. Smoke test: `test/t/35_vg_mpmap_trace.t`
(`cd test && prove -v t/35_vg_mpmap_trace.t`).

## PhaseUnfolder/prune concurrency (timing unvalidated)

Three merged commits, all ancestors of HEAD, attack `vg prune -u`'s
near-single-threaded bottleneck (measured 15.6-59h wall time per chromosome
regardless of `-t`, CPU% consistently 100-145%):

- `bbf264574` "Unfold complement components concurrently" — `src/phase_unfolder.{cpp,hpp}`
- `409c30a77` "Delete the pruned graph's paths in one call" — `src/subcommand/prune_main.cpp`
  only; despite what this file used to say, it does **not** touch `phase_unfolder.cpp`
- `b47de4db9` "prune: batch PackedGraph edge deletion" — `src/algorithms/prune.cpp`,
  `test/t/38_vg_prune.t`, and a `deps/libbdsg` gitlink bump to fork commit `b07563bb9`

Each records a byte-identity gate, so treat them as correctness-verified at fixture
scale. What is missing is a **pangenome-scale timing**: the A/B built to measure the
speedup (`prune_benchmarks/chr21_unfold_ab` in the `hprc_v2_vg_rna` workspace) aborted
5.5 minutes into its serial control arm and never ran the parallel arm. That abort is
explained — `Linger=no` killed the `systemd-run --user --scope` on session teardown
(exit 143); linger is enabled now. Quote no speedup until that A/B completes.

## `vg rna` transcript-path memory

Four merged commits (`4cd1c5dd3`, `583bfd90c`, `83c36ea51`, `28dc0b7be`) cut `vg rna`
peak RSS on the transcript route about 6x (chr20: 105.32 -> 15.31 GiB at `-t 1`),
byte-identical output, touching only `src/transcriptome.{cpp,hpp}`. The `augment()`
bypass is scoped to the transcript route — `vg rna -m` still calls `augment()` and
keeps the old profile. `vg rna` is byte-reproducible only at `-t 1`; above that,
thread completion order determines output node IDs. Method and results:
`docs/vg_rna_memory/README.md`.

## Tests

`test/t/33_vg_mpmap.t` and `test/t/35_vg_mpmap_trace.t` both pass on this machine.
Test 33 needs the BSD `rs` utility (installed here at `~/usr/local/bin/rs`); if it
is ever missing, tests 12/13 fail on a `command not found`, not on a vg change.
