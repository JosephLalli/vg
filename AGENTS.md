# Agent notes for this `vg` fork

This is a fork of `vgteam/vg` at release **v1.75.1** carrying three fork-specific
things, plus a smaller, partially-validated concurrency change to upstream prune
code (correctness gated at fixture scale; pangenome-scale absolute cost now
measured, no controlled speedup A/B yet). Read the linked docs before working on
any of them.

## Current-work routing and scientific phase transitions

**This supersedes the former rule to offer `/reload` before a new scientific
phase.** Do not run `/reload` automatically and do not create an approval gate.
Instead, invoke the documentation agent at each substantive phase transition.
It must reconcile the current tracked state, retained receipts, and live-process
state, then update these AI-facing notes with the intellectual state, code,
decisions, evidence paths, and unresolved acceptance work.

The current exact-only chr21 RNA/prune follow-up routes through
`docs/transcript_path_memory/IMPLEMENTATION.md`; generated experiment artifacts
live under `tmp/transcript_memory_20260915/`. Keep three efforts distinct:

The embedded-path GBWT-creation phase routes through
`docs/gbwt_creation/README.md`. User authorization now permits one guarded
production transition: migration PASS changed only guide. The optimized guide
and exact coverage check now pass; strip is running. The root owns the active
coordinator and successor follower. `prune_current` points to that follower,
which waits for pre-prune then runs ordinary prune before stop. No GCSA,
annotation-policy change, or chr2 speedup claim follows. Claude routing:
`docs/gbwt_creation/CLAUDE_HANDOFF.md`.

- **Code-performance work:** the parallel V3 full chr21 RNA result is terminal
  accepted: 18.538162 GiB peak RSS (1.30615977x OR), 33:31.11 wall, swap 0.
  Its independent checker passed `vg validate`, all six canonical graph/path
  fields (2,056,621 nodes, 2,726,485 edges and 5,607,688 named paths), exact
  info bytes and evidence guards. The raw `.pg` differs from the serial graph,
  so acceptance used the canonical semantic route. Receipts:
  `rna_parallel_output_v3/chr21-terminal/{acceptance.json,semantic/acceptance.json}`.
  The prior accepted serial V3 result, 18.5631 GiB/1.3079x OR and 54:11.56,
  remains the preserved baseline. V1 of the bounded parallel graph packer, info formatter,
  transcribed-node collector and missing-splice-edge discovery passed its
  guarded build and focused checks. Receipt:
  `rna_parallel_output_v1/build/acceptance.json`; binary SHA256
  `b0fa7af0cdb9864482bcd3eb764c4de7bc7a515d674c3c4cc38f396ab6192b38`.
  `rna_parallel_output_v1/checks/acceptance.json` records 48,455 assertions in
  49 cases plus real fixtures at 1/2/4/24 threads with identical semantics and
  info. The exact-byte dense ABBA averaged 28.1969 s serial versus 20.0230 s at
  24 threads, but only 1.326 CPU-seconds/wall-second; the useful-parallelism
  gate failed and no full parallel RNA run launched. V2 local-path microbatches
  and a bounded persistent output ring passed 42,497 assertions in nine
  standalone cases and retained exact dense output bytes. Its regular-file
  timing is inconclusive: the writer was sampled in `balance_dirty_pages` with
  `vm.dirty_bytes=100000000` while prune used the same SSD. The terminal
  `/dev/null` diagnostic (`rna_parallel_output_v2/discard/acceptance.json`)
  isolates packing: V2 T24 averaged 1.9822 s versus 7.2406 s for V1 T24 and
  20.2969 s for V2 T1. V2 local paths used 0.719/0.620 s wall and
  12.524/11.916 s CPU, about 18 useful cores. The terminal 500,039,680-step
  discard scale (`rna_parallel_output_v2/scale/acceptance.json`) then exposed
  coarse scheduling: local paths used 6.294 s wall/120.222 s CPU, while
  membership offsets used 29.274/35.926 and next links 31.666/44.002. V3 source
  now replaces those coarse membership producers with aligned microblocks and
  bounded stable-bucket waves. `rna_parallel_output_v3/BUILD_READY.json` pins
  seven source hashes. Header-level V3 acceptance is terminal: the standalone
  gate passed 42,512 assertions in 11 cases and both 50M outputs had exact hash
  `e390892b...c853f`; the 500M V2/V3 ABBA averaged 70.7286/20.7334 s (3.4113x
  packing-only), with offsets at about 18 effective cores, next links at about
  9.2 and local paths at about 20. A 2,056,621-node 50M-step T1/T24 pair was
  byte-identical; T24 used 1.7881 GiB RSS. The 500M V3 arms used at most
  2.6983 GiB and the planner stayed within 3 GiB. Receipts:
  `rna_parallel_output_v3/{standalone,post-header}/acceptance.json`. These gates
  compile current templates against the pinned V1 archive. The guarded V3 build
  is terminal PASS; binary SHA256
  `545de451f54b8bd291e4196e925dec4780f898bcb2d4bf96973b75d36b805f53`.
  The initial integrated gate retained a failure after 48,480 assertions in 52
  passing unit cases because its real-RNA T1 `.pg` bytes differed from the older
  gold; info, `vg validate`, and the canonical fingerprint matched. A same-binary
  T1 repeat then differed from the first output while matching the gold, and all
  three ID-preserving GFA exports matched exactly. The evidence localizes the
  difference to packed storage encoding/history, without isolating its precise
  cause; T1 byte reproducibility is not a guarantee. Same-input synthetic writer
  byte identity remains required. Receipts are
  `rna_parallel_output_v3/{fixture-diagnosis/acceptance.json,checks-completion-decision.json}`.
  Completion checks are terminal PASS with stable source/library/input guards;
  all 1/2/4/24 fixtures passed semantics, info, and validation, and the four
  same-input synthetic outputs were exact. The full parallel chr21 command is
  terminal success for invocation `769f682f1e2f4b0db7a33daad48d230e`:
  exit 0, 33:31.11 wall, 19,438,672 KiB = 18.538162 GiB = 1.30615977x OR peak
  RSS, swap 0, exact info, unchanged inputs, and a 38,126,287,944-byte streamed
  graph. `rna_parallel_output_v3/chr21/command-acceptance.json` closes the
  command and resource gates. The independent validator
  `vg-memory-chr21-rna-parallel-terminal-v3-20260915.service` (invocation
  `a64d62b5b6f04d0690d99bf3108f706f`) is terminal success. Its acceptance
  receipt passes `vg validate`, all six canonical fields, exact info, and stable
  evidence guards through the semantic fallback route. `chr21/phase-analysis.json`
  remains immutable command evidence binding 574 unchanged source hashes; its
  historical pending wording is superseded by the terminal receipt. The 38.1494%
  lower wall than the accepted serial
  V3 is an unmatched, nonexclusive-host comparison, not a controlled causal
  speedup. The remaining seam audit is `rna_parallel_output_v3/serial-followup.md`.
  The accepted result is the workable baseline. The isolated metadata-attribution
  pilot is terminal PASS (invocation `9c6d2ba45e3c40cb8c4b6aaac7ae94ad`)
  with source/library guards stable. Its sampled lookup estimates led to a
  private two-check removal. That lookup gate is terminal PASS (invocation
  `52f675bae8284b1fb8051468f5216ff8`):
  `rna_metadata_lookup_v1/checks/units/stdout` reports 42,622 assertions in 14
  cases, while `rna_metadata_lookup_v1/checks/acceptance.json` binds exact retained
  100,000-name bytes at T1/T24, stable guards, and a 5,607,688-name
  CPU-only ABBA reducing mean whole-writer wall from 93.4301 to 77.5504 s
  (17.0%), metadata from 87.3692 to 71.2887 s, and mean writer CPU from 128.2705
  to 113.113 s without increased peak RSS. The private prototype is now integrated into the live source tree
  after the current-binary prune proof gate closed. The integrated patch changes
  exactly `deps/libbdsg/bdsg/include/bdsg/internal/base_packed_graph.hpp` and
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
  concern. Shared-source translation and compaction timing remain optional later
  work.
  V2 evidence is summarized in
  `rna_parallel_output_v2/README.md`; V3 routing is in
  `rna_parallel_output_v3/README.md`.
  Use `-t` for independent work and measure wall time and CPU utilization along
  with RSS. Prune V5 terminated at its exact eight-hour deadline with systemd
  `Result=timeout`, status 15, and a 62 GiB peak. It produced no measured status,
  GNU-time receipt, or output graph, and received no semantic check. It is
  rejected as a performance or memory result; its preserved workspace is not an
  accepted output. Receipt: `chr21_prune/candidate-v5-terminal-timeout.json`.
  Ordinary V2 remains accepted at 75.608 GiB/1.80365x-OR and 4:14:01; the
  62.879 GiB 1.5x prune target remains unmet. Because V2 used an older XG and
  libbdsg binary, a frozen-current-binary ordinary revalidation is now gated in
  `prune_current_binary_v1/`. Its fixture service (invocation
  `dece0c5939e34d8d8eb19124001a0e9c`) is terminal PASS: TAP 26 and ordinary
  T1/T4/T24 plus real T24 all have exact graph/mapping bytes and validation, with
  stable guards. The full current-binary command invocation
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
  or authorize a speculative 1.5x run. Read its README and terminal receipts
  before advancing.
- **Whole-genome production indexing:** a separate acceptance path; do not turn
  fixture or candidate evidence into a production-index claim.
- **Annotation/paralog work:** a separate biological-policy effort; it does not
  inherit correctness or performance conclusions from this code work.

When resuming this area, read the implementation ledger first, then verify its
live-state references before acting. The ledger is owned by the active
implementation work; keep routing here compact.

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
keeps the old profile. Historical `-t 1` pairs were byte-identical, but the V3
same-binary diagnosis produced two semantically identical raw packings, so T1
byte reproducibility is not guaranteed. Above one thread, thread completion can
also determine output node IDs. Method and results:
`docs/vg_rna_memory/README.md`.

## Tests

`test/t/33_vg_mpmap.t` and `test/t/35_vg_mpmap_trace.t` both pass on this machine.
Test 33 needs the BSD `rs` utility (installed here at `~/usr/local/bin/rs`); if it
is ever missing, tests 12/13 fail on a `command not found`, not on a vg change.
