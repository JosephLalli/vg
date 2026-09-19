# GBWT creation performance phase

## Scope and current observation

This is a new **code-performance** investigation for creation of a GBWT from
embedded graph paths. It remains separate from annotation/paralog policy. The
user has now authorized one guarded chr2 production transition described below;
that authorization is limited to the `guide` replacement and does not change
the later ordinary-prune binary or policy.

**Current status:** private integration accepted; optimized chr2 guide and
guide check completed; strip and the three genic checks completed; the
prune-direct transition (below) removed the XG/distance stages and the live
stage is `mapping`, followed by ordinary prune in `prune_v3_direct_20260918T045514Z/`.
The retained private
binary is `integration/bin/vg-gbwt-insertion-threads`, SHA256
`645afde4cd7080f75b198de20329204fbdd3602a7286b71c14d26326f3a16b6e`.
Only six intentional source files changed; live `vg` and libhandlegraph remain
unchanged.

At the 2026-09-17 19:02 UTC observation, production unit
`chr2-preprune-guide-5368f900ccf9` was running `vg-pinned gbwt -p
--num-jobs 24 -E -x .../transcript_full.pg -o guide.gbwt` in the `guide` stage,
whose separately authorized follower proceeds through prune and then stops. The
stage manifest records 29,902,979
raw embedded paths. It was using about 1.1 effective cores, about 215 GiB
process RSS, and about 313 GiB cgroup memory, including about 97 GiB file cache,
under a 320 GiB cap with zero swap and no OOM events. These are observations of
one live production command, not a benchmark or a completed result.

The production roots are
`/mnt/ssd/lalli/hprc_v2_vg_rna/chr2_exact_corrected_prune_v1_current/`
(`index_preprune_current` and `prune_current`). Its follower request pins
libhandlegraph at `9f927257...dab250`; that production pin must be preserved.

## Authorized chr2 guide transition (guide complete; strip running)

The user explicitly authorized stopping the incomplete current guide process
PID 59119, unit `chr2-preprune-guide-5368f900ccf9`, and restarting **only** the
`guide` stage with the accepted private binary
`tmp/gbwt_creation_20260917/integration/bin/vg-gbwt-insertion-threads`
(SHA256 `645afde4cd7080f75b198de20329204fbdd3602a7286b71c14d26326f3a16b6e`).
At the transition observation it had run about 3:53 under its 320 GiB cap,
with about 218 GiB process RSS, no committed output, host MemAvailable about
448 GiB (481,117,589,504 bytes), and SSD free space about 10.58 TB. These are pre-stop observations,
not results of the optimized run.

The old coordinator, follower, and guide were stopped. Durable transition
evidence is
`.../index_preprune_v2_20260917T044500Z/transitions/optimized_guide_20260917T212027Z/STOP_RECEIPT.json`:
SIGTERM ended the guide after 14,371.786562 seconds; its measured summary,
eight completed receipts, and incomplete attempt are retained, the old PID is
gone, and there was zero swap/OOM.

`MIGRATION_ACCEPTANCE.json` is PASS: only guide changed, all eight completed
receipts are unchanged, the optimized binary and shared libraries were verified,
and config/plan SHA256 are
`fc41c1b4e5c889e48d8444f1b726cb8a2ea3c16bb74e9eafccd074d6122f8f5e` /
`2e22a44319b20b595148a0b222621adbb0b3a41f891ea7f61619d95f3cec5dd5`.
The runner supports the optional `guide_vg` pin/`--guide-vg` for fresh
preparation, PASSIVE for guide only, and a separate READY guide hash; seven
Docker pytest cases pass.

The historical `LAUNCH_RECEIPT.json` records active coordinator
`chr2-preprune-coordinator-opt-20260917t212558z` (PID 1915535) and follower
`chr2-prune-follow-opt-20260917t212558z` (PID 1915580). The replacement guide
unit `chr2-preprune-guide-27c4143016e9` was RUNNING at launch and is now complete.
`prune_current` atomically
points to `prune_v2_optimized_guide_20260917T212558Z`, which is
`WAITING_FOR_PREREQUISITES`, has stop `AFTER_PRUNE`, and supersedes the original
follower before prune. These are launch-state observations, not guide results.

The optimized guide is now terminal: `completed/guide.json` records passing
input guards, exit 0, a 3,238,845,424-byte GBWT (SHA256
`49e2de73af751b32bf9e95fe0ddf4ba450a9b6159cf36b9f47f6961b846c2e85`),
11,711.144588 seconds wall (3:15:11), 46,968.85 user + 2,800.38 system CPU
seconds (424%), 240,360,052 KiB (229.225208 GiB) GNU-time maximum RSS, and
334,310,895,616-byte (311.351 GiB) cgroup peak, with zero swap/OOM. `completed/guide_check.json` also passed: its exact sorted-name
comparison covered all raw RNA paths and wrote `coverage.txt: passed`; it used
157.160594 seconds and 8,730,716 KiB maximum RSS, with zero swap/OOM. The old
guide was interrupted, so these are absolute measurements, not a chr2 speedup
comparison. The current upstream `STATUS.json` is `RUNNING` at `strip`, unit
`chr2-preprune-strip-b30d48cc2210`; the coordinator and successor follower
remain active. The automatic authorized route remains pre-prune through ordinary
prune and then stop, with no GCSA.

Claude handoff routing is `docs/gbwt_creation/CLAUDE_HANDOFF.md`.

`RUNNING_VERIFIED.json` and current `STATUS.json` bind optimized PID 1916417,
guide unit/invocation `chr2-preprune-guide-27c4143016e9` /
`d1cb07be995b4b6cb5a760a529fc40f9`, `/proc/exe` SHA256 matching the accepted
binary, `OMP_NUM_THREADS=24`, PASSIVE, a 320 GiB no-swap cap, CPU quota 24, live
metrics, unchanged receipt hashes, successor follower pins, and absent old PID.
Its roughly 33.1 GiB memory.current at about 50 seconds is graph-loading state,
not a peak or performance result.

All stages other than guide retain their existing validated binaries and pins.
After the optimized guide reaches the existing prerequisite receipt, the
follower may run its already-authorized ordinary prune and then stop. No GCSA,
annotation-policy change, full RNA rerun, or full-chr2 performance claim is
authorized by this transition.

## Prune-direct transition (2026-09-18; user-owned sequence change)

The user transferred ownership of the chr2 workflow ("fuck the handoff, it's
your project now. alter the sequence"). After `strip` (1:54:12, 213.80 GiB),
`genic_validate` (1:07:43, 204.93 GiB), `genic_stats` (9:34) and `genic_paths`
(32:06) sealed, the coordinator sat in `WAITING_FOR_RESOURCES` for `xg`
because two 256 GiB Docker competitors left about 200 GiB admissible. Both
idle supervisors were stopped at 04:55 UTC with no stage running
(`transitions/prune_direct_20260918T045514Z/STOP_RECEIPT.json`; 14 receipt
hashes unchanged), and `migrate.py` removed `xg`, `xg_validate`, `xg_paths`,
`xg_stats`, `distance` and `distance_check` from the pinned recipe
(`MIGRATION_ACCEPTANCE.json` PASS; surviving stage dicts byte-identical;
new pinned runner SHA256 `64382f24...1a7f`). `launch.py` prepared the successor
follower `prune_v3_direct_20260918T045514Z/`, repointed `prune_current`, and
started `chr2-preprune-coordinator-direct-20260918t045514z` (PID 1257721) and
`chr2-prune-follow-direct-20260918t045514z` (PID 1257735) with
`RuntimeMaxSec=30d`; `mapping` was admitted at 05:34:24 UTC once the
`allocator_substitution` container exited.

Rationale, as corrected by adversarial review: `vg prune -u` reads neither an
XG nor a distance index, `--xg-name` is a warned no-op, and prune builds its
own XG in-process (`src/subcommand/prune_main.cpp:441-511`), so a standalone
`vg index -x` before prune constructs the same XG twice. No `vg index -x` RSS
measurement exists for the pinned binary at any scale. The chr19 receipts
(`vg convert -x` 8.07x and `vg index -j` 5.31x the genic.pg file size) were
produced by vg v1.74.1, whose node-to-path index used a disk-spilling mmmulti
map that the pinned binary does not contain (the fork's uncommitted `deps/xg`
rewrite is retained as `deps-xg-working-tree.diff`); an earlier projection of
~1300 GiB from those receipts is withdrawn. The same-lineage chr21 prune anchor
(74.815 GiB on a 34.52 GiB genic.pg, 2.167x, a peak over all phases including
the XG build) scales to roughly 350 GiB on chr2 for both prune and a standalone
`vg index -x`; treat it as a scaling estimate from one chromosome, not a
reproduction. Prune's five-second samples will provide the first real chr2
XG-construction measurement. XG/distance for mapping remain a separate design
question: mpmap needs both, the distance index can be built from a
path-stripped graph (snarl decomposition is topology-only), and no
reference-only XG is derivable from genic.pg because it carries only the
14,952,173 transcript paths.

Standing hazard: `bin/vg-pinned` resolves `lib/libhandlegraph.so` from this
worktree by RPATH (not RUNPATH, so `LD_LIBRARY_PATH` cannot redirect it), and
the run pins that file by hash and inode/mtime. `./build-local.sh` now refuses
to build while `.build-freeze` names a receipt that does not yet exist.

The chr2 prune is terminal: 12:47:16 wall, 341.07 GiB GNU-time peak RSS against
the 480 GiB cap, zero swap/OOM, exit 0; `prune_check` passed and
`PRUNE_COMPLETE.json` records `gcsa_executed: false`. Outputs are
`chr2.pruned.pg` (6,774,195,898 bytes; 50,647,839 nodes, 53,787,207 edges) and
`chr2.mapping` (340,858,176 bytes). Per-phase profile:
[../prune_scheduling/README.md](../prune_scheduling/README.md).

Cap change (same day): prune at 512 GiB was not admissible while the user's
count container held a 256 GiB cap (later 200 GiB by the user's choice; its
7-day peak is 163.7 GiB). The prune cap became a follower-request parameter
(`run_chr2_prune.py prepare --prune-memory-gib`, tested), and the user chose
480 GiB after review showed the gate admitted up to 493 GiB and that 448 sat
below the 350-450 GiB planning band. `prune_v4_cap480_20260918T055521Z/`
superseded the idle v3; prune stage `chr2-preprune-prune-5f82c69e8cac` was
admitted and started at 08:24 UTC. Record:
`transitions/prune_cap448_20260918T055521Z/` (directory named before the
cap was raised; `LAUNCH_REQUEST.json` records `cap_gib: 480`).

The chr2 prune's per-phase profile, the withdrawal of the chr19-derived XG
projection, and the unfold scheduling work it justifies are in
[../prune_scheduling/README.md](../prune_scheduling/README.md).

## Code boundary and unknowns

The pinned old production binary's observed `-E` path entered
`src/subcommand/gbwt_main.cpp`'s embedded-path branch and called
`config.haplotype_indexer.build_gbwt(*graphs.path_graph)`. The original
`PathHandleGraph` overload supplied no job-count argument and
`GBWTBuilder` had one construction worker. The integrated private source now
passes `build_jobs` from `-E` while preserving the old `HaplotypeIndexer`
overloads. The restarted chr2 guide uses the accepted private executable. The
original worktree `bin/vg` and binaries used by other production stages remain
unchanged.

Generated experiment and private-build artifacts belong under
`tmp/gbwt_creation_20260917/`; implementation paths are listed in the integrated
private-source checkpoint below. Fixture inputs, test binary, and linked
libraries must be isolated and hash-pinned; live production binaries, libraries,
and inputs are read-only evidence for this phase. Use `./build-local.sh` for any
future build.

## Isolated constructor attribution (2026-09-17)

An isolated Docker build used `./build-local.sh` and the retained `probe.cpp`,
`prepare_profile.py`, `probe.mk`, `profile-pins.json`, and `build-profile.log`
under `tmp/gbwt_creation_20260917/`. The profile object and control executable
used separately pinned libraries; `profile-pins.json` binds the relevant source,
probe, static libraries, and libhandlegraph SHA256 `9f927257...dab250`.

The smoke control and profile runs used 1,000 synthetic paths of length 128.
They produced identical GBWT bytes; reloading checked every forward and reverse
sequence slot and metadata count. The 100,000-path, length-512, 128-locus,
100-million-node synthetic batch had 44,801,300 forward steps. Control and
profile again had exact output bytes. The profiled build was 10.4858 wall /
10.4761 CPU seconds at 859,524 KiB peak RSS; control was 11.0510 / 11.0506 at
862,428 KiB. This is instrumentation-only attribution, not a speed comparison.

The profiled batch reports record construction 2.92347 s, next-link construction
0.09676 s, sort 2.60765 s, offset construction 1.44988 s, and advance 2.33215 s.
The earlier 10-million-node batches likewise put record construction near 60% of
the profiled loop time. These synthetic phase shares are not chr2 proportions:
they omit real graph traversal, production input layout, page-cache effects, and
all other command phases. Host `perf` attachment was denied because
`perf_event_paranoid=4`; no host setting was changed.

## Private merged-sort prototype (not accepted for integration)

The private deferred-count and merged-sort candidate passed its synthetic
semantic gate. The 100-million-node synthetic ABBA mean build time fell from
11.03015 to 2.686935 seconds (4.1051x); output bytes were exact and all seven
per-iteration traces matched across control, deferred, and T1/2/4/24 candidate
arms. Twenty compressed and dynamic cases also matched. This is a valid result
for that synthetic constructor workload only.

It does not demonstrate an improvement for the repeated real RNA walks. The
actual 432-path RNA source is
`tmp/transcript_memory_20260915/rna_metadata_lookup_v1/integration/checks/rna-t1/measured/stdout`, SHA256
`b319dc14689ccb8fb55a48f225d53e7a6925ac056cf5c9c9e9a42030ac61f911`.
Repeating those walks 200 times produced 86,400 paths and 56,775,600 forward
steps across 18,579 insertion iterations per batch. Control took 6.24691 wall /
6.269 CPU seconds; the T24 candidate took 6.16814 / 23.618. Hence there is no
demonstrated wall-time speedup for this workload, while CPU consumption increased
substantially. Do not generalize the synthetic 4.1051x result to real use.

`real/chr21-fixture.gaf` is a historical mislabel for the 100,000-singleton
metadata-stress input. Retain it, but use `real/rna432.gaf` for the actual
accepted RNA fixture. At this prototype stage, integration was unaccepted:
it lacked an explicit bounded insertion-thread API and `-E` wiring, small-batch
serial retention, OpenMP/background-thread exception propagation, an
ABI-consistent private dependency and vg rebuild, and CLI fixture acceptance.
The later private-source checkpoint below supersedes those implementation
deficits but not the retained fixture limitation or delivery gate.

The original TSAN run had a startup mapping failure; the diagnostic container's
`setarch -R` run instead reported an OpenMP stack reuse warning in
`tsan-noaslr/stderr`. This diagnostic changed no host setting and did not
establish concurrent implementation safety; it is superseded by the later
instrumented-application Archer gate below.

## Diverse-cohort fixture and Archer gates

The repeated-432 experiment repeats identical walks and therefore does not
measure construction as diverse GBWT records accumulate. The authorized sampler
completed: a deterministic systematic sample of 100,000 embedded paths across
the accepted full chr21 V3 stored path order preserves sampled names,
orientations, relative order, and biological/retention-selection counts. It has
78,242,469 forward steps. `cohort/acceptance.json` is terminal PASS and binds
the compact reusable fixture.

The source is
`tmp/transcript_memory_20260915/rna_parallel_output_v3/chr21/measured/stdout`:
38,126,287,944 bytes and 5,607,688 paths. Its terminal receipt binds the path,
size, inode, mtime, and semantic hashes, but has no raw `.pg` SHA256. Do not hash
or reread the entire graph merely to manufacture one. The helper used
`PackedGraph::deserialize` for one full 38 GB input load. The sampler was first
validated against the accepted 432-path graph and its existing GAF walks.

The first cohort screen has exact GBWT bytes and reloads in every arm. Profile
build time was 38.1898 wall / 38.5702 CPU seconds at 1,917,356 KiB peak RSS;
scalar-stable was 34.8417 / 34.9452 at 1,912,916 KiB; stable T24 was 22.8347 /
79.7068 at 1,846,632 KiB. These are first-screen observations, not a cohort ABBA
claim. The earlier synthetic and repeated-432 limitations remain preserved and
are superseded only as prior next-gate wording.

`archer-gate/acceptance.json` is terminal PASS for the instrumented application:
the positive race control is detected, the race-free control passes, and stable
candidate repeats pass at T2/T4/T24. It makes no race claim about uninstrumented
OpenMP internals. This supersedes the prior open Archer gate; the retained TSAN
startup-mapping and stack-reuse diagnostics remain historical evidence.

The live binary and libraries remain read-only. This is fixture-scale
construction validation, not a full RNA or chr2 run and not an annotation-policy
change.

## Integrated private-source checkpoint

The source now has explicit per-builder `GBWTBuilder::insertion_threads`
(default 1), deferred per-record workers, scalar-stable sort skip, persistent
exception poison, and `-E` `build_jobs` forwarding. The ABI changed, so every
constructor caller requires rebuilding and the new header must take precedence.
`integration/build-receipt.json` passes with unchanged source and live-library
guards; its first failed attempt is retained because a stale root
`include/gbwt/dynamic_gbwt.h` required an overlay-first include order.

`integrated-probes/acceptance.json` is PASS on the chr21 100k cohort
(78,242,469 steps): original mean build time was 40.53525 s and integrated mean
was 27.12945 s (1.494x; 33.07% lower). Outputs and full reloads were exact;
candidate RSS was 1.7513--1.7720 GiB versus 1.8206--1.8215 GiB. Twenty
adversarial cases at T1/2/4/24 match the original. This supersedes the earlier
42.26→25.33 GNU-parallel sort prototype, which was not the integrated algorithm.

The scalar sort is retained because GNU parallel sort allocates within OpenMP and
cannot propagate `bad_alloc`. `integration/faults/` passes deterministic record
and offset throws; it verifies exact typed messages, persistent poison, no
recode after failure, new-batch behavior, and safe destruction. Archer passes
T2/T4/T24 three times after startup confirmation for the instrumented
application only. The terminal `integration-fault-acceptance.json` is PASS for
the two deterministic faults and nine Archer arms, binding the prior positive and
negative runtime controls, checker files, source/live guards, generated-source
hashes, runtime presence, and linkage. The full-build-ready receipt is PASS.
`src/unittest/gbwt_builder.cpp` covers 10,000 paths, multiple batches, sampling,
and overflow. The failed 20:30:38 and 20:31:42 UTC CWD attempts are retained as
missing-fixture failures, alongside prelaunch 20:30:22 UTC; they are not code
failures.

The single unset-OMP default-wait screen passed with exact bytes at 26.6936 wall
seconds and 176.471 CPU seconds. `OMP_WAIT_POLICY=PASSIVE` used about 94 CPU
seconds at similar runtime; this is not a controlled policy claim.

`INTEGRATION_ACCEPTANCE.json` is terminal `PASS_PRIVATE_INTEGRATION`. It binds
unchanged full-build/source/live guards and all source, input, binary, and shared
library pins. It records no obsolete three-argument constructor symbol, 240,076
unit assertions in three cases, TAP 37 with all 168 checks passing, and RNA-432
CLI control/candidate outputs at T1/2/4/24. All eight outputs are byte-identical
(`c0734c...fc52ab`) and their 432 names exactly preserve input order. The CLI
receipts are under `integration/acceptance-attempt-20260917T203223Z/`.

## Required acceptance before a performance claim

A candidate needs separately pinned control and candidate executables and
libraries, with the same frozen graph and options except the tested threading.
Record wall time, CPU time
and effective CPU use, process RSS, cgroup memory split (anonymous and file
cache), peak, swap, OOM events, and relevant I/O.

Correctness must establish the same named oriented walks, in the same required
order, with identical GBWT metadata and all required serialized outputs. Where
the format permits nonsemantic storage variation, retain an explicit
format-aware comparison that proves those invariants; do not substitute a size,
runtime, or graph-only comparison. The acceptance receipt must bind source,
library, binary, input, command, and output hashes.

The authorized private code phase is complete. The prior prohibition on a
full-chr2 production action is superseded only by the guarded guide transition
above. The 1.494x result remains a bounded 100k-cohort subset result and
full-chr2 performance is unmeasured. GCSA work and annotation changes remain
out of scope. Parallel-sort work is optional future work only, requiring a
separate phase, profiling evidence, and exception-safe design.
