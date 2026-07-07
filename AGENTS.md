# Agent notes for this `vg` fork

This is a fork of `vgteam/vg` at release **v1.75.1** carrying two fork-specific
things. Read the linked docs before working on either.

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

## 2. The `vg mpmap` splice-search instrumentation

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

## Tests

`test/t/33_vg_mpmap.t` and `test/t/35_vg_mpmap_trace.t` both pass on this machine.
Test 33 needs the BSD `rs` utility (installed here at `~/usr/local/bin/rs`); if it
is ever missing, tests 12/13 fail on a `command not found`, not on a vg change.
