# AGENTS.md

Orientation for agents working in this repository. **`CLAUDE.md` is the single
source of truth**; this file exists only to point at it and is deliberately
short. If the two ever disagree, `CLAUDE.md` wins — and the disagreement is a
bug worth fixing immediately, because both are read at session start.

## What this repository is

A fork of `vgteam/vg` at release **v1.75.1**, carrying five fork-specific
subsystems plus small upstream patches needed to compile here. Everything else
is stock upstream `vg`.

## The rule that matters most

**Keep three efforts distinct: vg code performance, production indexing, and
annotation/paralog policy.** A result in one authorizes nothing in the others.
Most of the confusion this project has produced traces to blurring them.

Two corollaries that have each cost real time:

- **Do not attribute a speedup without a controlled A/B.** An absolute timing,
  an uncontrolled before/after, and a projection are each weaker claims. All
  three have been misread as the strong one in this repository's history.
- **A projection is not a measurement.** Two projections here were later
  falsified by measurement by factors of 3.5x and 6x. State the input class a
  projection was derived from, or do not state the projection.

## Where to read

| You are about to | Read first |
|---|---|
| Build anything | `CLAUDE.md` → "Building"; never a bare `make`, and note the build freeze and submodule hazard |
| Change prune or PhaseUnfolder | `CLAUDE.md` → "PhaseUnfolder/prune concurrency", then `docs/prune_scheduling/README.md` |
| Change `vg rna` | `CLAUDE.md` → "`vg rna` transcript-path memory", then `docs/vg_rna_memory/README.md` |
| Change GCSA2 | `deps/gcsa2/EXTERNAL_MEMORY_CONSTRUCTION.md` |
| Change mpmap tracing | `MPMAP-SPLICE-TRACE.md` |
| Understand current state | `CLAUDE.md` → "Current state" |
| Look up a receipt, invocation ID or ABBA timing | `docs/transcript_path_memory/IMPLEMENTATION.md` (the ledger), or `docs/gbwt_creation/README.md` for the chr2 index run |

## Gates that apply to every change here

- Byte-identity gates run at `-t 1`. Above one thread, compare
  relabel-invariant digests — node IDs depend on thread completion order.
- Editor and LSP diagnostics are not authoritative; only a real
  `./build-local.sh` compile decides whether something builds.
- Temp-heavy work sets `TMPDIR` to an SSD path, never the default HDD.
