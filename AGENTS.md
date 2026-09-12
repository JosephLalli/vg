# Agent notes for this `vg` fork

This is a fork of `vgteam/vg` at release **v1.75.1** carrying three fork-specific
things, plus a smaller, partially-validated concurrency change to upstream prune
code (correctness gated at fixture scale; pangenome-scale absolute cost now
measured, no controlled speedup A/B yet). Read the linked docs before working on
any of them.

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

## PhaseUnfolder/prune concurrency (absolute cost measured; no controlled A/B)

Three merged commits, all ancestors of HEAD, attack `vg prune -u`'s
near-single-threaded bottleneck (measured 15.6-59h wall time per chromosome
regardless of `-t`, CPU% consistently 100-145%):

- `bbf264574` "Unfold complement components concurrently" — `src/phase_unfolder.{cpp,hpp}`
- `409c30a77` "Delete the pruned graph's paths in one call" — `src/subcommand/prune_main.cpp`
  only; despite what this file used to say, it does **not** touch `phase_unfolder.cpp`
- `b47de4db9` "prune: batch PackedGraph edge deletion" — `src/algorithms/prune.cpp`,
  `test/t/38_vg_prune.t`, and a `deps/libbdsg` gitlink bump to fork commit `b07563bb9`

Each records a byte-identity gate, so treat them as correctness-verified at fixture
scale. The controlled A/B built to measure the speedup
(`prune_benchmarks/chr21_unfold_ab` in the `hprc_v2_vg_rna` workspace) aborted
5.5 minutes into its serial control arm and never ran the parallel arm. That abort is
explained — `Linger=no` killed the `systemd-run --user --scope` on session teardown
(exit 143); linger is enabled now — but the A/B itself is still not rerun, so still
quote no speedup multiple for these three commits.

What does now exist (2026-09-12) is a pangenome-scale **absolute** timing on the real
build binary, not a controlled A/B: chrY/chr18/chr2 genic prune at 6.05/42.28/194.65
GiB peak RSS and 11:42/38:26/2:41:33 wall, scaling linearly in graph size (chrY is a
small-graph outlier, not a scaling anchor) and projecting the 23-chromosome corpus at
~33h serial against the ~22-day plan it replaces. chr2's CPU profile (mean 518%, peak
791%) is the first pangenome-scale evidence `bbf264574` engages. A before/after against
`bbf264574`'s own commit note (4h18m -> measured 2h41m, within 3% of the Amdahl
prediction on the unfold alone) is suggestive but is a different-binary comparison, not
a controlled A/B, and does not replace `chr21_unfold_ab`. Memory, not time, is now the
binding constraint: chr2 peaked at 194.65 GiB of a 280 GiB cap. Detail: `CLAUDE.md`'s
"Fork-specific subsystems" section and `hprc_v2_vg_rna/notes/prune_calibration_2026-09-12.md`.

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
