# Small reviewer fixtures

These fixtures support the [contribution proposals](https://github.com/JosephLalli/vg/issues/11).
They live on a separate review branch so that benchmark setup and receipts do not
become production interfaces or enlarge each implementation PR.

[Earlier stage evidence](PERFORMANCE.md) preserves this review kit's existing
receipts. [Current command and stage measurements](COMMAND-PERFORMANCE.md)
reports the 2026-10-07 comparisons, source provenance, variation and tradeoffs.
The new measurement/debugging helpers, generated fixtures and raw results are
local temporary shims and are not part of this published review kit.

All inputs are generated text or small source-built graphs. There is no private
chromosome input, saved binary index, frozen object archive or production job.
The scripts use Python's standard library, GNU `time`, and the separately built
candidate tools. Linux process limits keep each command below 2 GiB of address
space; elapsed-time limits prevent an unattended fixture from becoming a large run.

## Check out and build matching sources

Use the head and immutable comparison base shown on the relevant PR. Build each
checkout with its own recorded submodule pins and the project's supported build
procedure. Do not mix an installed header from one revision with another archive.

Personal-fork pins require local URL overrides before initializing submodules:

```sh
git config submodule.gcsa2.url https://github.com/JosephLalli/gcsa2.git
git config submodule.deps/gbwt.url https://github.com/JosephLalli/gbwt.git
git config submodule.deps/libbdsg.url https://github.com/JosephLalli/libbdsg.git
git config submodule.deps/xg.url https://github.com/JosephLalli/xg.git
git submodule update --init --recursive
```

These are local checkout settings. The production `.gitmodules` still points to
the owner repositories; an upstream consumer pin must follow an accepted owner
commit. Native checks use `vg test`, for example:

```sh
path/to/vg test '[transcriptome],[edited_mapping],[shared_transcript_path]'
path/to/vg test '[prune],[phaseunfolder]'
path/to/vg test '[snarl_distance]'
```

The recorded environment was Ubuntu 22.04, GCC 11.4, fresh sources and matching
dependencies. Local builds went through the required `build-local.sh` wrapper in
a separate 24 GiB container. That machine-specific wrapper is not a dependency of
these fixtures. The libbdsg Makefile itself selects C++20 while preserving
C++17-clean public headers. Platform CI remains a separate gate.

## Complete command comparisons

```sh
python3 review/fixtures/make_fixtures.py review/fixtures/inputs
python3 review/fixtures/run_commands.py --mode rna \
  --vg baseline=/absolute/path/to/baseline/vg \
  --vg candidate=/absolute/path/to/candidate/vg \
  --fixtures review/fixtures/inputs --output review/fixtures/outputs/rna
```

Use `--mode prune` or `--mode gcsa` for the other command checks. RNA covers
embedded paths, GBWT reference paths, mutation and projection at one/four threads.
One-thread bytes must match. Threaded checks compare path-anchored topology,
sequences, tables and reloaded GBWT walks because node IDs can depend on completion
order. The GBWT fragment input is deliberately separate from embedded graph paths.

Prune checks fresh and appended mappings at one/two/four threads. Its existing
verifier aborts on the recorded appended-mapping input in both upstream and
candidate; append checks therefore compare actual graph/mapping results, and only
fresh checks invoke the verifier. GCSA compares resident/external ordinary GCSA and
LCP files, query verification and empty scratch directories.

```sh
python3 review/fixtures/run_gcsa_native.py --builder /absolute/path/to/build_gcsa \
  --fixtures review/fixtures/gcsa-inputs --threads 4 \
  --output review/fixtures/outputs/gcsa-native
python3 review/fixtures/run_library_callers.py --mode gbwt \
  --baseline /absolute/path/to/baseline/vg --candidate /absolute/path/to/candidate/vg \
  --output review/fixtures/outputs/gbwt
```

The native GCSA script covers empty/zero-doubling/cyclic inputs, FD limit 64,
reloads, too-small memory, blocked output paths and cleanup. The library-caller
script also accepts `--mode xg`; it compares ordinary index bytes and reloaded
walks. GBWT uses 10,000 mixed-orientation paths and workers 1/2/4/24, crossing the
parallel insertion threshold.

## Stage measurements

The C++ probes use actual production types or APIs. Compile the RNA representation
probes against the indicated R03/R08/combined source headers, rather than a copied
type definition. Each target requires its own explicit checkout and rebuilds on
every invocation, so a changed header cannot silently leave an old executable:

```sh
make -C review/fixtures/rna-probes R03_CHECKOUT=/absolute/path/to/R03 \
  r03-representation-benchmark
make -C review/fixtures/rna-probes R08_CHECKOUT=/absolute/path/to/R08 \
  representation-benchmark
make -C review/fixtures/rna-probes COMBINED_CHECKOUT=/absolute/path/to/combined \
  translation-cache-probe
python3 review/fixtures/run_representation_probes.py
```

After a matching full vg build, `extra-targets.mk` links the graph-output and
distance probes against that checkout's libraries. Load it after the normal
Makefile, setting `REVIEW_FIXTURES` to this directory and `REVIEW_ID` to the arm name.
It does not change the normal vg build targets.


For each separately built stock-based R16/R17 checkout, compile the output probe
with `REVIEW_ID=serial` or `parallel`. The default source is
`standalone_serialization_benchmark.cpp`; the older shared-source probe is only
for the separately recorded storage/output integration.

```sh
# Run in the separately built candidate checkout, with absolute fixture paths.
make -f Makefile -f /absolute/path/to/review/fixtures/extra-targets.mk \
  REVIEW_FIXTURES=/absolute/path/to/review/fixtures REVIEW_ID=serial \
  review-serialization-benchmark
python3 /absolute/path/to/review/fixtures/run_stage_comparisons.py \
  --mode serialization --baseline /absolute/path/to/serialization-serial \
  --candidate /absolute/path/to/serialization-parallel \
  --output /absolute/path/to/outputs/serialization
```

Use the normal supported compiler/environment for that checkout. In the recorded
local environment, the same target invocation goes through `./build-local.sh`.
The distance targets are `review-distance-fixture` (both arms) and
`review-distance-staging` (D01). Pass their executables with `--mode distance`,
`--baseline`, `--candidate` and `--staging`. Prune uses `--mode prune`, two complete
vg executables, and `--fixtures` pointing to its generated 8,000-component input.

`run_stage_comparisons.py` runs three ABBA cycles. Its serialization mode measures
final output separately from setup and requires ordinary graph byte identity.
Distance mode compares 1,328 exhaustive serialized/reloaded query records before
timing dense and sparse staging with the unchanged vg map reservation. An input
digest alone is not the equivalence test. Prune mode measures the complete command
and compares graph manifests and mappings; generate its input with
`make_fixtures.py --components 8000`.

Run timing comparisons after builds stop and review available RAM, CPU and SSD
space first. Process peak RSS includes setup. A stage result does not establish
a whole-command speedup, and a synthetic command result does not predict a genome
run. The evidence files preserve exact source heads, pins, binary hashes and scope.

## OpenMP race check

The B1 race receipt uses Clang 14, LLVM OpenMP 14 and LLVM's Archer tool, with
`GBWTBuilderTest.InsertionThreads` at workers 0/1/2/4/24. The instrumented sources
were compiled with ThreadSanitizer; no production source workaround was introduced.
`OMP_TOOL_LIBRARIES` selected a locally built Archer from the recorded official
LLVM 14 source. A clean OpenMP barrier control passed; a deliberately unsynchronized
write control produced a race report. The tool's active initialization was checked
for both controls and the insertion test.

Archer models OpenMP synchronization for ThreadSanitizer. An inactive Archer or
uninstrumented GCC `libgomp` cannot substitute for that check. The recorded runtime
required disabling ASLR inside an isolated test container; no host-wide setting
changed. `tsan-controls/control.cpp` supplies the positive and negative controls.
This bounded check supplements owner tests and supported-platform CI.
