# Claude handoff: vg performance and chr2 indexing

**TERMINAL as of 2026-09-18.** The authorized chr2 workflow is finished: ordinary
`vg prune` and its checks completed and `PRUNE_COMPLETE.json` exists at
`ANNOTATION/prune_v4_cap480_20260918T055521Z/`. No chr2 unit is running. GCSA2 was
not run and is not authorized. Nothing in this file requires action; it is kept as
the record of how the run was routed. Current state and results:
`docs/gbwt_creation/README.md`, `docs/prune_scheduling/README.md`, and the
transition directories under `PREP/transitions/`.

---


Take over this work from Codex. Continue the already-authorized chr2 workflow
through ordinary `vg prune` and its output checks, then stop. Preserve the
running services, completed outputs, failed attempts, and resumable workspaces.
Recheck live state before acting: this snapshot was verified at approximately
**2026-09-18 02:20 UTC / September 17, 21:20 America/Chicago**.

## Read first

VG worktree (`VG` below):
`/mnt/ssd/lalli/.codex/worktrees/e3e42ac4-cc4f-4bc9-850a-591d197313b9/vg-latest`

Consumer workspace (`CONSUMER`): `/mnt/ssd/lalli/hprc_v2_vg_rna`

1. Read `VG/CLAUDE.md`, `VG/AGENTS.md`, and
   `VG/docs/gbwt_creation/README.md`.
2. Read `CONSUMER/CLAUDE.md`, `CONSUMER/WORKSPACE_STATE.md` Section 29, and
   `CONSUMER/notes/chr2_exact_annotation_index_status_20260916.md`.
3. Consult `VG/docs/transcript_path_memory/IMPLEMENTATION.md` for accepted
   RNA/prune performance work. Read `VG/docs/vg_rna_memory/README.md` before
   editing RNA, and `VG/deps/gcsa2/EXTERNAL_MEMORY_CONSTRUCTION.md` before
   editing GCSA2. Do not reopen closed experiments just to refresh context.

Keep three efforts distinct: **vg code performance**, **production indexing**,
and **annotation correction/paralog policy**. Chr2 production is authorized;
whole-genome execution and GCSA2 continuation are not part of this task.

## Production locations and current state

Annotation root (`ANNOTATION`):
`/mnt/ssd/lalli/hprc_v2_vg_rna/chr2_exact_corrected_prune_v1_20260916T015932-0500`

Preparation (`PREP`): `ANNOTATION/index_preprune_v2_20260917T044500Z`

Prune continuation (`PRUNE`): `ANNOTATION/prune_v4_cap480_20260918T055521Z`
(prune cap 480 GiB by user decision; supersedes `prune_v3_direct_20260918T045514Z`
at 512 GiB and `prune_v2_optimized_guide_20260917T212558Z`, both retained as
`SUPERSEDED_BEFORE_PRUNE`; transition record
`PREP/transitions/prune_cap448_20260918T055521Z/`, run with `--cap 480`)

The convenience links are `CONSUMER/chr2_exact_corrected_prune_v1_current`,
then `index_preprune_current` and `prune_current`. Verify their targets.

**Superseded on 2026-09-18 (prune-direct transition).** The user transferred
ownership of this workflow and the sequence was altered: `strip`,
`genic_validate`, `genic_stats` and `genic_paths` all sealed, then the six
XG/distance stages were removed from the plan (see
`PREP/transitions/prune_direct_20260918T045514Z/README.md` for the rationale,
receipts and rollback). The remaining preparation stage is `mapping`
(admitted 05:34:24 UTC); `PREP/READY_FOR_PRUNE.json` then releases the
successor follower.

- Preparation coordinator: `chr2-preprune-coordinator-direct-20260918t045514z.service`, PID 1257721.
- Prune follower: `chr2-prune-follow-cap480-20260918t055521z.service`, PID 2409758;
  prune stage `chr2-preprune-prune-5f82c69e8cac` RUNNING since 08:24 UTC under a
  480 GiB/no-swap cap and 30 h deadline; supervisor `RuntimeMaxSec=30d`.
- The previous coordinator/follower pair (`...-opt-20260917t212558z`) was stopped
  idle at 04:55 UTC; no stage unit was running. Their inactive state is not a failure.

Read `PREP/STATUS.json`, `PRUNE/STATUS.json`, the current stage's service, and
`PREP/completed/*.json` before launching anything. Do not create duplicate jobs.
**`PRUNE/PRUNE_COMPLETE.json` is the requested final boundary.** Prune (512 GiB
cap) is admitted only once competitor-unused caps fall below about 70 GB, which
at the time of writing means the `pancollapse-real-native-20260916T022706Z`
container exiting or its cap shrinking; the supervisor waits automatically.

Do not run `./build-local.sh` in the vg worktree until `PRUNE_COMPLETE.json`
exists (`.build-freeze` enforces this): the run pins `lib/libhandlegraph.so`
by inode.

## Completed results

- Exact annotation selection, projection, correction, and repaired full-input
  corrected-annotation validation are accepted. Reuse this generation.
- RNA: 4,606.8169 s (1:16:46.8), 107.1778 GiB peak RSS, no swap; graph
  `PREP/artifacts/rna/transcript_full.pg`, 178,160,252,278 bytes.
  SHA256 `e158b58c7cde230c362bb07094040d998e943453dd073a419da70c1ad1d233b6`.
- Partition: 29,902,979 paths = 14,952,173 biological + 14,950,806 retention.
  RNA validation and stats passed. The graph has 9,520,546 nodes and
  13,798,365 edges.
- **Optimized guide:** exit 0, measured wall **11,711.144588 s (3:15:11)**,
  GNU-time peak RSS **240,360,052 KiB = 229.225208 GiB**, CPU 424% averaged
  over the command, zero swap/OOM/guard failure. Cgroup peak was
  334,310,895,616 bytes, including cache; distinguish it from process RSS.
- Guide: `PREP/artifacts/guide/guide.gbwt`, 3,238,845,424 bytes,
  SHA256 `49e2de73af751b32bf9e95fe0ddf4ba450a9b6159cf36b9f47f6961b846c2e85`.
- `guide_check` passed in 157.160594 s: the original accepted binary loads
  the guide, and its complete sorted path-name list matches RNA's info table.
  This production check establishes loading/name coverage, not an independent
  full-chromosome comparison of every encoded walk against the interrupted run.

Authoritative receipts: `PREP/completed/{rna,partition,rna_validate,rna_stats,guide,guide_check}.json`.
Each points to its attempt, exact command, sealed output identities/hashes, and
measured summary. Guide measurements are in
`PREP/attempts/guide-27c4143016e9/{summary.json,time.txt,stderr.log,samples.jsonl}`.
Five-second samples retain process/thread CPU, RSS, I/O, cgroup counters,
output growth, and log offsets. Some cgroups lack `io.stat`; retain that
limitation instead of silently presenting missing counters as zero.

## What changed in GBWT

The original `vg gbwt -E --num-jobs 24` route did not pass its requested worker
count into embedded-path construction and ran near one effective core.
The implementation adds explicit per-builder `insertion_threads` (default 1),
parallel record/offset work with deferred updates, and `-E` job-count forwarding.
Stable sorting remains serial, with an already-sorted fast path. Small batches
remain serial. Worker exceptions are captured and persistently poison a failed
builder. Other construction routes retain their previous default threading.

Implementation files in VG:

- `deps/gbwt/include/gbwt/dynamic_gbwt.h`, `deps/gbwt/src/dynamic_gbwt.cpp`
- `src/haplotype_indexer.hpp`, `src/haplotype_indexer.cpp`
- `src/subcommand/gbwt_main.cpp`, `src/unittest/gbwt_builder.cpp`

Accepted private binary:
`VG/tmp/gbwt_creation_20260917/integration/bin/vg-gbwt-insertion-threads`

SHA256: `645afde4cd7080f75b198de20329204fbdd3602a7286b71c14d26326f3a16b6e`.
Production uses its identical pinned copy `PREP/bin/vg-guide-optimized` only
for guide construction, with 24 workers and `OMP_WAIT_POLICY=PASSIVE`.

`VG/tmp/gbwt_creation_20260917/INTEGRATION_ACCEPTANCE.json` is the private
acceptance index: 240,076 assertions in three unit cases, all 168 TAP checks,
RNA-432 CLI equivalence at T1/2/4/24, adversarial constructor equivalence,
injected-failure checks, and Archer race checks. The retained 100k-path chr21
subset improved 40.53525 → 27.12945 s (1.494x) with exact output bytes.
**Do not claim that speedup for full chr2:** the old chr2 guide was interrupted
after 14,371.786562 s, so no completed matched chromosome control exists.

The GBWTBuilder ABI changed. The copied root `VG/include/gbwt/dynamic_gbwt.h`
was stale; the accepted build used an overlay-first header and rebuilt all
constructor callers. Follow `integration/build-private.sh` and its receipts
for any further private build. Never run bare `make`; use `./build-local.sh`.

## Pins, restart history, and operational constraints

All other production stages use `PREP/bin/vg-pinned`, SHA256
`4f495d705c5547a39d1334a9c6cd7d4e02ece9e50fe65831ea79ba2679b4273c`.
The resolved live `VG/lib/libhandlegraph.so` is pinned at
`9f927257f4933f45fdb890f13be0abddea196027af1bdfa1b7ac352700dab250`.
Do not replace either while the production workflow depends on them.

Transition directory:
`PREP/transitions/optimized_guide_20260917T212027Z/`

It preserves old config/plan/runner/status/follower records and the interrupted
guide attempt. `MIGRATION_ACCEPTANCE.json` proves that only guide changed and
all eight previously completed stage receipts were unchanged. `migrate.py` is
one-time and already passed: **do not rerun it**. `RUNNING_VERIFIED.json` is a
historical launch observation; the newer completed receipts supersede it.

The consumer runner `scripts/transcript_graph/run_chr2_preprune.py` now supports
a separate pinned `guide_vg` (`--guide-vg` for fresh preparation), leaves other
commands unchanged, and records a separate guide hash in READY. Seven focused
Docker tests passed. `run_chr2_prune.py` and `measured_stage.py` provide the
continuation and sampling. Active work uses pinned copies in each run's `bin/`.

The fresh follower request binds the migrated config/plan. The old follower
directory `ANNOTATION/prune_v1_20260917T165200Z` is preserved and superseded;
do not restart it. Exact replacement coordinator commands are retained in
`PREP/transitions/optimized_guide_20260917T212027Z/LAUNCH_REQUEST.json`.
Resume only after inspecting service/lock/attempt state. The executor attaches
to surviving stages and skips sealed results; do not manually fabricate seals.

Admission reserves active competitors' unused finite caps plus 64 GiB, requires
2 TiB SSD free, and watches 48 GiB memory headroom. Ordinary prune has a
512 GiB/no-swap cap and 30-hour deadline; checks have 96 GiB and six hours.
These are limits, not estimates. At this snapshot, host available memory was
about 453 GiB and SSD free about 9.6 TiB. Recheck before any new launch; leave
admission decisions to the existing guard. Native user services persist with
lingering enabled; no terminal session needs to stay open.

## Git state and the next action

VG branch: `rna-copy-elimination`, HEAD
`9a377ca9ca41d6aa71c33add6fa2038d3b95c1b3`.
Consumer branch: `de-dockerize-vg-execution`, HEAD
`dedeaac33eecdd1a87c67a47539fb117e5130d6d`.
Both are dirty with concurrent work and modified submodules. Accepted private
artifacts and source hashes matter more than HEAD alone. Preserve uncommitted
work; do not reset, clean, rebuild shared libraries, or switch branches blindly.

Start by checking live STATUS, services, and the latest receipt. Let the
authorized workflow advance; investigate a concrete failure without discarding
its evidence. Report stage, elapsed time, CPU use, process RSS versus cgroup
memory, and remaining work. Once prune and checks complete, reconcile terminal
metrics and documentation and stop before GCSA2. No additional experiment is
needed merely to recreate already accepted evidence.

Runtime matters alongside memory. The accepted chr21 RNA result (33:31.11,
18.538 GiB) is a workable baseline; the accepted current-binary chr21 prune
result is 3:21:53 and 74.815 GiB. Do not resume rejected disk-heavy or timed-out
experiments without a new justification. Annotation/paralog changes are a
separate biological-policy effort; keep this accepted chr2 annotation fixed.

Before a substantive new scientific phase, run a bounded documentation agent
and review its reconciliation of code, decisions, experiments, outputs, results,
and remaining acceptance work. This replaces offering `/reload` and creates
no extra approval gate. Continue already-authorized work autonomously.
