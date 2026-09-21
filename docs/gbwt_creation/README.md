# GBWT creation performance phase

## Scope and current observation

This is a new **code-performance** investigation for creation of a GBWT from
embedded graph paths. It remains separate from annotation/paralog policy. The
user has now authorized one guarded chr2 production transition described below;
that authorization is limited to the `guide` replacement and does not change
the later ordinary-prune binary or policy.

**Current status, as of the 2026-09-17 transition below:** private integration
accepted; optimized chr2 guide and guide check completed; strip and the three
genic checks completed; the prune-direct transition (below) removed the
XG/distance stages and the live stage was `mapping`, followed by ordinary
prune in `prune_v3_direct_20260918T045514Z/`. This has since moved past
`mapping`: the "Prune-direct transition" section below records that prune
itself went on to run and is now terminal (12:47:16, 341.07 GiB peak, exit 0),
matching `CLAUDE.md`'s chr2 status. This paragraph is left as the
point-in-time state at the guide/strip transition rather than updated in
place, so it does not contradict the terminal result below; read the
"Prune-direct transition" section for the current state. The retained private
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
measurement exists for the pinned binary at any scale as of this 2026-09-18
transition; a 2026-09-20 chr21 measurement fills that gap and is reconciled
against the anchor below in "Guide GBWT downstream products and path sense"
further down this file. The chr19 receipts
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
14,952,173 transcript paths. A 2026-09-20 chr21 measurement adds a third route
for the distance index specifically -- built directly from a GBZ, with no XG
at all, at 7.5x less peak RSS -- while leaving the XG question for mpmap's own
graph argument open; see "Guide GBWT downstream products and path sense"
below.

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

## Guide GBWT downstream products and path sense (chr21 evidence, 2026-09-20)

New measurements, made 2026-09-20 on the pinned production binary (SHA256
`4f495d705c5547a39d1334a9c6cd7d4e02ece9e50fe65831ea79ba2679b4273c`) against the
chr21 exact-dedup arm at
`/mnt/ssd/lalli/hprc_v2_vg_rna/notes/evidence/chr21_exact_arm_20260914/exact/`
(`genic.pg` 37,068,417,249 bytes, 34.52 GiB; `guide.gbwt` 611,729,792 bytes;
5,607,688 named paths; 2,056,621 nodes / 2,726,485 edges) -- chr21, not chr2.
Full receipts and their adversarial verification are
`docs/exact_dedup_indexing_feasibility/RECEIPTS.md` and
`docs/exact_dedup_indexing_feasibility/GBZ_INDEXING_SURVEY.md`. This belongs in
this file because the input is the same guide-GBWT product this file's
"Authorized chr2 guide transition" section tracks, and the path-sense finding
below is a property of how this project names guide-GBWT paths, not of GBZ
construction generally.

### A GBZ and an r-index build cheaply from a completed guide GBWT

`vg gbwt -x genic.pg -g chr21.gbz guide.gbwt` took 3:30.59 wall, 48,602,136 KiB
(46.35 GiB) peak RSS, exit 0, and produced a 622,882,936-byte GBZ -- 59.5x
smaller than the 37,068,417,249-byte `genic.pg` PackedGraph it was built from,
and only 11,153,144 bytes larger than `guide.gbwt` alone (that delta is the
34,459,835 bp node-sequence payload). `vg gbwt -r guide.gbwt` (the r-index)
took 40.62 s, 3.37 GiB peak, and produced 565,177,017 bytes.

The same input also gave this project's first `vg index -x` RSS measurement on
the pinned binary at any scale, which the "Prune-direct transition" section
above (2026-09-18) recorded as absent: 54:28.44 wall, 71,850,656 KiB
(68.52 GiB) peak RSS, producing a 55,013,545,597-byte XG. That 68.52 GiB is
lower than, and should not be read as reproducing, the 74.815 GiB "chr21 prune
anchor" that same section cites -- the anchor is a peak over the whole prune
process including its in-process XG build, the new figure is a standalone
`vg index -x` on the same genic.pg, and the two are not the same measurement
even though both are chr21 and both land in the 60-80 GiB range. Against the
GBZ, the XG took 15.5x more wall (54:28.44 vs 3:30.59), 1.48x more peak RSS
(68.52 vs 46.35 GiB), and is 88.3x larger on disk (55,013,545,597 vs
622,882,936 bytes); with the r-index included, GBZ+`.ri` (1,188,059,953 bytes)
is still 46.3x smaller than the XG. Each side of this comparison ran once, with
no repeat and no host-contention control, so treat these as single-measurement
ratios, not noise-floor-cleared results. The measured `xg/pg` byte ratio here
is 1.484, against the 1.427 the whole-genome driver had assumed from an
OR-dedup graph -- 4% apart, so that driver's sizing assumption stands with a
small correction rather than a withdrawal.

None of the ratios above should be booked as a pipeline-wide saving by
themselves: this project's downstream `rpvg` step requires an XG regardless of
what mpmap uses, exiting "Graph (xg format) input required" when none is
given, and prepared `joint_mpmap_rpvg_*` runs already exist in the downstream
`hprc_v2_vg_rna` workspace. Wherever rpvg stays in the pipeline, the XG is
deferred, not eliminated.

Distance-index construction was measured arm-to-arm on the same chr21 input:
from the GBZ, 2:31.32 wall, 8.06 GiB peak; from the XG, 3:45.47 wall,
60.59 GiB peak -- 7.5x less peak RSS and 1.49x less wall from the GBZ, at 5.2x
more CPU-seconds (1,010.6 vs 195.6), which is GBZ-load cost, not parallel
construction. One run per arm, host under load, no noise floor measured; the
7.5x memory ratio is far outside plausible contention and is credited, the
1.49x wall ratio is not and is held unconfirmed. `fill_in_distance_index` takes
a bare `const HandleGraph*` (`src/snarl_distance_index.hpp:34`) and never
needed an XG; the positional form (`vg index -j out.dist graph.gbz`) must be
used, since `-x` is hard-typed to `xg::XG` at
`src/subcommand/index_main.cpp:778` and fails on a GBZ. This is the one saving
in this whole set of findings usable today with no fixture-scale caveat and no
contract break: build the distance index from a GBZ, not the XG, whenever a
GBZ already exists. It resolves only the distance-index half of the
"Prune-direct transition" section's open XG/distance question; mpmap's own
graph argument (below) is a separate question and remains open.

Two further bounds on this result. The first has since been partly lifted: on
2026-09-20 `vg mpmap` opened a GBZ-built `.dist` on chr21 and its startup fell
from 27:29.23 to 19:17.85 at unchanged peak RSS, so for the GBZ route this is
no longer only a construction-cost result -- though the XG-built `.dist` has
still never been opened, and five reads measure startup, not throughput. And the GBZ this index was built from is not
topologically identical to the source graph: `vg stats -N -E` counts
2,701,234 edges on `chr21.gbz` against 2,726,485 on the exact-dedup source
graph -- 25,251 fewer, 0.926%, because a GBWTGraph's edge set is only what its
threads support. Whether that gap changes any distance-index answer, and
whether it matters for the splice-junction edges this fork's
`--trace-splice-search` work looks for, is unmeasured; "no contract break"
describes the build step, not topology equivalence with a distance index built
from the full graph.

These per-artifact costs are chr21 measurements on a 34.52 GiB `genic.pg`.
chr2's guide GBWT (3,238,845,424 bytes, "Authorized chr2 guide transition"
above) is 5.29x larger by GBWT-file size than chr21's 611,729,792 bytes, and no
GBZ, r-index, standalone XG, or distance index has been built from chr2's
guide GBWT or `genic.pg`. No chr2 wall-time or peak-RSS figure for any of these
artifacts is claimed here.

### Replacing the guide stage with `vg rna -b -g`: unmeasured, and vg rna's own peak would rise

`GBZ_INDEXING_SURVEY.md` ranks combining the guide-GBWT build into
`vg rna -b -g` as its second-highest-ranked available saving, because it would
remove a separate stage and its full re-read of chr2's 173.58 GB
`transcript_full.pg`. That saving is not established by anything measured so
far in this file: the "Authorized chr2 guide transition" section's
3:15:11 wall / 229.23 GiB peak guide build (240,360,052 KiB GNU-time maximum
RSS) used the private `vg-gbwt-insertion-threads` binary at `--num-jobs 24`,
and the 1.494x speedup this file's own "Integrated private-source checkpoint"
section records for that binary is a property of its multi-worker
`GBWTBuilder`. `Transcriptome::add_transcripts_to_gbwt`
(`src/transcriptome.cpp:3998-4023`), which is what `vg rna -b -g` would call
instead, is a single serial `gbwt_builder->insert` loop -- the private binary's
threading does not carry over to it. Net wall-clock change from adopting
`vg rna -b -g` is unmeasured and could be negative, and `vg rna`'s own peak RSS
would rise by the cost of building a `DynamicGBWT` in-process for chr2's
29,902,979 paths -- the opposite direction from this fork's `vg rna`
transcript-path memory work (`docs/vg_rna_memory/README.md`). Two further
contract breaks are unresolved: `-g` becomes mandatory, or GBZ construction
aborts with `InvalidGBWT`; and the metadata shape changes -- `vg paths -L -g`
prints `tx1_R1#0#0#0` instead of `tx1_R1` -- so the production `guide_check`
step's exact sorted-name comparison, which passed for the optimized guide per
"Authorized chr2 guide transition" above, would need rewriting, not just
rerunning, before this route could be accepted. This is not authorized and no
acceptance run has been attempted; it is recorded here as an identified option
with its costs, not a plan.

### Path sense is a naming property of this project's guide GBWT, and it is the most actionable finding here

`guide.gbwt`'s 5,607,688 named paths split as 1,398,636 REFERENCE-sense,
4,209,052 GENERIC-sense, and 0 HAPLOTYPE-sense (`vg paths -x chr21.gbz -L`,
cross-checked against `vg gbwt -Z --tags`, which lists all 230 non-CHM13 HPRC
samples under `reference_samples`). Sense is assigned purely by name:
`get_sample_sense` (`deps/gbwtgraph/src/utils.cpp:174-188`) maps the magic
generic-sample name to GENERIC, any name listed in the `reference_samples` tag
to REFERENCE, and everything else to HAPLOTYPE. The GENERIC 4,209,052 split as
2,803,513 `__panSC_retention_pad1000__<sha256>_{L,R}_R1` retention-pad walks
plus 1,405,539 other non-PanSN names
(`docs/exact_dedup_indexing_feasibility/RECEIPTS.md` section 13) -- GENERIC
because none of these names carry a PanSN `#` field, not because of any tag.
The retention pads alone are 66.6% of the GENERIC set, not all of it. The
REFERENCE 1,398,636
are ordinary PanSN transcript names
(`<sample>#<hap>#<sample>_{ha,pa}_T<id>_R1`) whose sample happens to be one of
the 230 listed in `reference_samples`.

`--set-reference` cannot repair this for a transcript-only guide, because it
only moves a REFERENCE-tagged sample to HAPLOTYPE (or back) and never touches a
GENERIC name. Rebuilding with
`vg gbwt -x genic.pg --set-reference CHM13 -g chr21.ref.gbz` (2:07.91 wall,
46.24 GiB peak, 622,881,552 bytes) produced 0 REFERENCE and left all 4,209,052
GENERIC in place -- a 25% cut in the overlay's input, not a fix, because CHM13
is not a sample this guide GBWT contains and the retention pads were never
REFERENCE to begin with.

Every GBZ built from this project's guide GBWT -- chr21's today, and chr2's or
any other chromosome's once their guides exist -- inherits this sense
assignment, because it is fixed by the guide GBWT's path names before any GBZ
is ever built, and no downstream `vg gbwt` or `vg mpmap` flag changes it. The
fix, if made, is a naming convention applied when the guide GBWT itself is
built: give only the chromosome reference sequence a name `get_sample_sense`
resolves to REFERENCE (or list it in `reference_samples`), and give every
transcript and retention-pad walk a name that resolves to HAPLOTYPE rather than
GENERIC. No such convention is implemented or accepted anywhere in this project
yet; this section records the diagnosis, not a fix, and the fix belongs at this
file's stage of the pipeline if it is taken up.

The operational cost this sense assignment causes is measured and is large
relative to what an 88.3x-smaller-than-XG artifact would suggest:
`vg mpmap -x chr21.ref.gbz -g chr21.gcsa` (no `-d`) on a 5-read smoke test took
27:29.23 wall and 162,068,556 KiB (154.55 GiB) peak RSS, of which 14.3 of the
27.2 startup minutes (3.3 to 17.6 min) is `overlay_helper.apply()`
(`src/subcommand/mpmap_main.cpp:1830-1831`) building a `PackedPositionOverlay`
over every REFERENCE-or-GENERIC path -- all 4,209,052 GENERIC survive even with
`--set-reference CHM13` applied, since GENERIC is untouched by that flag.
`RECEIPTS.md` section 13 records that an earlier working note in this same
investigation had concluded the overlay was not the mechanism; that was wrong
and stands retracted there. Neither the 27:29.23 wall figure nor the
154.55 GiB peak characterizes `vg mpmap` mapping cost on a GBZ -- both are
dominated by this startup overlay, not by aligning the 5 reads, which all
mapped at MAPQ 60, with genuine multipath structure on 2 of the 5. No
controlled `vg mpmap -x chr21.xg` run exists, so there is no baseline the
154.55 GiB can be checked against.

### The strip stage and dropping `-r`/`--add-ref-paths`

The "Prune-direct transition" section above already records chr2's strip-stage
cost: 1:54:12 wall, 213.80 GiB peak. `GBZ_INDEXING_SURVEY.md` ranks dropping
`vg rna -r`/`--add-ref-paths` as its highest-ranked available saving, because
it would eliminate that stage outright rather than make it faster, and would
empty the path payload from the XG that `vg prune -u` builds in-process -- on
chr2, specifically the "XG path structures, then path release" phase, which is
33.2% of the 12:47:16 total wall and holds a 336.89 GiB path plateau
(`docs/prune_scheduling/README.md`, phase table). How much of that 33.2% is
path-payload cost versus fixed XG-construction cost was not measured
separately, so no specific fraction of the saving is claimed -- only that the
whole phase becomes unnecessary if there is no path payload to strip out of in
the first place.

This is verified only at fixture scale, 37 nodes: the stripped and unstripped
arms gave identical `vg stats -N -E -l` (40/34/224) and identical
node-sequence and outdegree multisets, but different duplicate node IDs
(49 vs 50, 43 vs 41) and a different `.mapping` file. A byte-for-byte or
ID-for-ID comparison will therefore report a false failure at any larger scale;
acceptance must use this project's relabel-invariant digest set (name-sorted
FASTA, GBWT path name/length/step-count, node-sequence multiset,
`vg stats -N -E -l -z`), the same standard this file already requires for
above-`-t 1` byte-identity gates. Fixture scale, 37 nodes, is many orders of
magnitude below chr2's 50,647,839-node pruned graph, so this is not yet a
chr2-scale result.

Dropping `-r` is not a standalone change. With no embedded transcript paths in
`genic.pg`, `vg gbwt -E` has nothing left to build a guide GBWT from, so the
guide would have to come from `vg rna -b -g` (previous subsection, itself
unmeasured for net wall and rising in peak), and an XG built from a pathless
`genic.pg` would carry no transcript path information for mpmap to use.
Dropping `-r`, adopting `-b -g`, and using the GBZ as mpmap's graph argument are
one proposed change to chr2's (and future chromosomes') indexing route,
evaluated above from three angles, not three independent options that could be
adopted piecemeal.

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
