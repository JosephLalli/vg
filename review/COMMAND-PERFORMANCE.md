# Current command and implementation-stage measurements

These 2026-10-07 measurements close the selected current-source fixture gaps in
the [proposal series](https://github.com/JosephLalli/vg/issues/11). They supplement
the earlier [stage and representation evidence](PERFORMANCE.md). New measurement
drivers, debugging helpers, generated inputs, and raw receipts remain local.
This page publishes human-readable results and provenance only.

## Method and limits

Each accepted comparison ran three serialized ABBA cycles: six observations per
arm, twelve in total. Tables report medians; wall-time ranges and cycle medians
below expose variation. Fixture generation and builds were outside timing. No
review builds or other review benchmarks ran concurrently. Other users' load on
the shared host remained uncontrolled; ABBA reduces order effects without
eliminating background-load effects. No result estimates a genome-scale saving.

The environment was the pinned Ubuntu 22.04 vg CI image, GCC 11.4, CPUs 232–247,
a 24 GiB container limit, an 8 GiB per-process address-space limit, SSD scratch,
and a timeout of at most 600 seconds per arm (120 seconds for the accepted GCSA caller). GCSA stages additionally used FD 128. The
construction allowance is a working-buffer target, not a process RSS limit.
GNU `time` captured complete-process wall time, peak RSS, and user/system CPU;
the RNA runner captured wall time and RSS only. Internal stage timers exclude
fixture preparation and post-stage digest/reload checks. Process RSS includes
the helper's setup, measured work, and verification.

vg command pairs used the same `build-local.sh` recipe and C++20/O3 configuration.
GBWT library drivers used matching C++17/O3 owner archives and helpers. Optional
GCSA stage comparisons used matching C++17/O2 owner archives; paired helper
flags also matched. The codec helpers used O3 on both sides. The foundation's
resident/external comparison used the same O3 executable for both routes.

## Complete vg RNA commands

| Proposal and route | Threads | Wall seconds, baseline → candidate | Peak RSS MiB, baseline → candidate |
| --- | ---: | ---: | ---: |
| R03, haplotype reference | 1 | 18.670 → 9.935 | 1910.3 → 666.8 |
| R07, haplotype mutation | 1 | 2.895 → 2.870 | 496.8 → 483.3 |
| R08, haplotype mutation | 1 | 20.175 → 20.095 | 840.7 → 925.5 |
| R08, node-aligned reference exons | 1 | 8.245 → 8.520 | 590.9 → 394.0 |
| R09, long reference | 1 | 1.430 → 1.450 | 566.0 → 559.0 |
| R10, haplotype mutation | 1 | 3.465 → 3.430 | 464.0 → 464.0 |
| R11, haplotype mutation | 1 | 19.330 → 18.350 | 925.3 → 567.9 |
| R16, embedded-path projection | 1 | 11.440 → 9.480 | 610.3 → 559.8 |
| R17 over R16, embedded-path projection | 4 | 7.490 → 6.175 | 764.2 → 759.8 |

R03 substantially reduced runtime and memory on this input. R07/R09/R10 did not
establish a meaningful complete-command runtime benefit. R08 has a tradeoff:
mutation increased peak RSS by about 85 MiB with nearly unchanged time, while
node-aligned reference exons saved about 197 MiB and took about 3% longer.
R11's memory reduction was substantial, but one of its three cycle medians was
slower. R16/R17 improved time in all three cycles; their complete-command results
include the selected library implementation and caller, rather than an isolated
library-commit benefit. Existing priorities and performance holds are unchanged.

Inputs were independently validated synthetic GFAs with every P/W walk adjacent
in the graph. The scaled mutation/reference input had 4,000 reference nodes,
4,083 total graph nodes, 3,000 annotations, and approximately 6.564 million
annotated transcript steps. The medium input had 1,600 reference nodes, 1,633
total nodes, 1,200 annotations, and approximately 1.26 million annotated steps.
The long reference input had 250,000 reference nodes, 255,155 total nodes, 1,000
annotations, and approximately 1.05 million annotated steps. Estimated annotated
steps describe the input workload, not the exact count after all projections.

The timed commands had this common shape:

```sh
vg rna -t THREADS -c no -r -n ANNOTATIONS [ROUTE_OPTIONS] \
    -f OUTPUT.fa -i OUTPUT.tsv -b OUTPUT.gbwt INPUT.pg > OUTPUT.pg
```

Reference options were `-l INPUT.gbwt -j -o`; mutation options were
`-l INPUT.gbwt -j -k 3 -d`; embedded projection used `-e -a -k 3 -d`.
R03/R08/R11 emitted 3,000 FASTA/TSV/GBWT records, R07/R10 emitted 1,200, and
R09 emitted 1,000. R16/R17 emitted 3,425: 1,200 reference and 2,225 projections
from the two valid sample paths. Serial arms required identical graph, sequence,
TSV, and GBWT bytes. R17 required path-anchored graph/sequence equivalence, sorted
TSV equivalence, and reloaded oriented GBWT walks; numeric node IDs may vary with
thread completion order.

## GBWT, XG, and distance commands

| Comparison | Measured work | Wall seconds | Peak RSS MiB | Total process CPU seconds |
| --- | --- | ---: | ---: | ---: |
| B2, stock release → new library and caller | `vg gbwt -E --num-jobs 4` | 2.150 → 1.920 | 408.0 → 410.0 | 2.145 → 3.810 |
| B2, same library; serial → four-worker caller | `vg gbwt -E --num-jobs 4` | 1.990 → 2.190 | 410.0 → 410.0 | 1.980 → 4.115 |
| B2, same library; longer paths, serial → four workers | `vg gbwt -E --num-jobs 4` | 6.070 → 6.605 | 725.9 → 724.0 | 6.060 → 14.480 |
| XG-vg, combined XG implementation | `vg index -t 4 -x` | 26.720 → 7.560 | 705.9 → 248.3 | 19.855 → 3.820 |
| D01-vg, sparse alternative snarl | `vg index -t 4 -j` | 0.020 → 0.020 | 28.0 → 28.0 | 0.050 → 0.050 |
| D01-vg, dense cyclic snarl | `vg index -t 4 -j` | 0.415 → 0.205 | 68.0 → 32.0 | 0.465 → 0.240 |

The B2 stock comparison is a combined library-plus-caller result. Its shorter
wall time costs more CPU. Holding the library fixed reverses the wall result on
this short-path fixture: forwarding four workers is about 10% slower and uses
about twice the CPU. The combined result therefore does not establish a benefit
from forwarding itself. A second same-library check increased path length from
195 to 771 steps while retaining 50,001 paths: 38,550,771 total embedded steps
over 3,596 nodes and 384 eight-way bubbles. Four workers remained about 9%
slower in all three cycles and used about 2.4 times the CPU. Both regressions
are reported; neither fixture supports recommending worker forwarding as a
performance improvement. Representative diverse real inputs remain necessary
for that claim. XG-vg measures the retained combined XG constructor;
it establishes no separate speedup for X1, X2, X3, or X4.

GBWT/XG used 50,001 mixed-orientation paths and 9,750,195 total steps over 1,004
nodes, with 96 eight-way bubbles and a 128-wide alternative. Exact index bytes
matched and reloaded walks matched all input paths and their multiplicities.
Distance fixtures had 1,001 paths, 19,019 total steps, and ten snarls: the sparse
graph had 116 nodes and a 32-wide alternative, and the cyclic graph had 596 nodes
and a 512-wide internal ring. Each arm passed 131,072 reloaded distance queries,
including unreachable pairs. The sparse case is too short for a speed claim;
the dense result is limited to this bounded input. An earlier 2,048-wide acyclic
pilot failed an upstream-baseline query assertion and contributes no timing.

The complete-command invocations were:

```sh
vg gbwt -E --num-jobs 4 -o OUTPUT.gbwt -x INPUT.pg
vg index -t 4 -x OUTPUT.xg INPUT.pg
vg index -t 4 -j OUTPUT.dist INPUT.pg
```

## GBWT library insertion and serial control

The current B1 comparison used its actual PR base, GBWT
`14a06d588ea6de50cd809f546b9bb57c92c15ac1`, against
`6cabe5fc2cef04d922d68b561d190b0c7ee02bd8`. The local driver inserted
50,000 paths × 195 forward steps: 9.75 million forward and 19.5 million
oriented steps, with a 100-million-node batch target. Exact serialized bytes
matched, and every forward/reverse path matched after reload.

| Comparison | Insertion-stage seconds | Complete-helper wall seconds | Peak RSS MiB |
| --- | ---: | ---: | ---: |
| Old serial pipeline → B1, four workers | 1.620 → 1.343 | 8.055 → 7.805 | 311.3 → 313.1 |
| Old serial pipeline → B1, one worker | 1.535 → 1.024 | 7.950 → 7.425 | 311.3 → 313.3 |

Save/reload/verification dominated the helper at about 6.3 seconds per arm and
was outside the insertion timer. Complete-process CPU was 8.055 → 9.750 seconds
for the four-worker comparison and 7.945 → 7.415 seconds for the serial control.
CPU was not timed separately inside insertion. B1 improves serial insertion too;
these results do not attribute its entire benefit to parallel workers. The two
batches are separate source comparisons, not a direct one-versus-four-worker A/B.
An earlier stock-release-base comparison is retained locally; the immediate-parent
comparison above supplies the PR attribution.

## GCSA construction and implementation stages

| Proposal / comparison | Measured scope | Time seconds | Process peak RSS MiB |
| --- | --- | ---: | ---: |
| GCSA-1, resident → external | Complete native construction; verification separate | 60.210 → 39.720 | 32.0 → 29.4 |
| GCSA-9, resident → external | Complete vg caller construction; verification separate | 49.250 → 5.520 | 47.3 → 50.4 |
| GCSA-2 | Typed fixed-record sort | 2.050 → 1.811 | 18.0 → 18.0 |
| GCSA-3A | First external prefix-doubling join | 10.874 → 9.236 | 117.5 → 117.9 |
| GCSA-3B over 3A, matched four shards | First external prefix-doubling join | 2.480 → 3.265 | 109.8 → 124.0 |
| GCSA-4 | Rolling group window plus forced spill/readback | 6.723 → 0.051 | 4.0 → 4.0 |
| GCSA-5A | Framed versus raw path-record sort | 3.630 → 1.924 | 31.2 → 30.9 |
| GCSA-5B over 5A | Prefetch path-record sort | 2.074 → 1.839 | 30.0 → 43.7 |
| GCSA-6 over 3B | Write prepared final-component event streams | 1.756 → 0.211 | 69.6 → 80.0 |
| GCSA-8 over foundation | Verification only; 900,000 repeated records | 0.965 → 1.320 | 31.0 → 4.0 |
| GCSA-LCP over 6 | Complete construction; admitted LCP overlap | 21.435 → 22.130 | 201.3 → 244.7 |

Foundation construction used 64 cyclic walks of 272 bases, k=16, 17,408 input
rows, and 15,215 distinct verification patterns. Both routes used one thread
and four doubling rounds; the external allowance was 16 MiB. All twelve paired
ordinary GCSA/LCP files were byte-identical, reloaded, and passed all patterns.
Verification was timed separately, at about 0.08 seconds per arm. Complete-
process CPU medians were 42.400 → 23.480 seconds. This small
cyclic input establishes neither a genome-scale memory reduction nor a universal
external-construction speedup. Sampled scratch and `/proc` I/O observations were
lower bounds, rather than complete disk-I/O accounting.
The medians of sampled per-arm scratch peaks were 0.975 → 1.307 MiB;
the largest observed peaks were 1.133 → 3.109 MiB. Median last-observed
`/proc` physical read/write counters were 494.6/427.6 → 502.5/438.2 MiB.
These are sampled observations, excluding fixture generation and independent
verification; brief peaks and final I/O can be missed. They establish no
reduction in physical I/O, filesystem-cache use or total required disk capacity.


The native foundation command used text input and the following options;
only the external arm added the memory option:

```sh
build_gcsa -t -T 1 -D SCRATCH -d 4 -o OUTPUT INPUT_PREFIX
build_gcsa -t -T 1 -D SCRATCH -d 4 -o OUTPUT --memory-limit 16M INPUT_PREFIX
```

The LCP comparison used the same command shape with `-T 4` and
`--memory-limit 512M` in both arms. Verification-only GCSA-8 used
`build_gcsa -t -T 1 -D SCRATCH -L -v --memory-limit 1M -o INDEX INPUT_PREFIX`.
The internal-stage comparisons called the existing implementation stages from
local drivers; they were not new production entry points or instrumented
production builds.

GCSA-9 used the same vg executable for both routes, on a separate four-path
subset of the native input: 1,088 binary k-mer rows and 951 distinct verification
patterns. Construction used one thread, four doubling rounds and a 120-second
arm timeout. The command was:

```sh
vg index -t 1 -g OUTPUT.gcsa -i INPUT.graph -k 16 -X 4 -b SCRATCH
# The external arm added: --gcsa-memory 16M
```

The deprecated binary-k-mer input option was used for this local comparison
because it excludes graph-to-k-mer generation. There was no built-in `-V` inside
the timed command: an independent local converter reloaded each GCSA/LCP pair
and verified all 951 patterns afterward. All twelve ordinary index pairs were
byte-identical and scratch was empty. Complete-process CPU was 38.840 → 1.730
seconds; peak RSS increased by about 3 MiB. The larger 64-path resident vg caller pilot
stalled before producing an index, including without `-V`; its zero-byte output
files were permission-check placeholders. The native builder handled that larger
binary input. A subsequent single external-caller correctness run also completed
on all 64 paths: both output hashes matched the native canonical files, all
15,215 patterns passed after reload, and scratch was empty. This is larger
external-route correctness evidence, not a controlled timing comparison. The
larger resident baseline remains unresolved; the four-path comparison establishes
neither a larger-input caller speedup nor a general one.

GCSA-2 sorted a four-million-key uint64 permutation at a 16 MiB allowance and
merge fan-in eight. Both arms used multiple runs; every output key was checked
against the independent sorted-value oracle. One typed-sort cycle was slower;
its lower overall median does not establish a consistent wall-time benefit.
Complete-helper CPU medians were 1.330 → 0.770 seconds.
GCSA-3A used a 64 MiB working allowance and 557,056 valid k-mer
rows and canonicalized resulting path/rank records. Its wall result varied:
one cycle was slower, although process CPU medians fell from 4.710 to 3.490
seconds. The matched GCSA-3B comparison gives both arms the same four contiguous
139,264-row inputs. All candidate arms admitted four workers, and the same
557,056-record / 1,671,168-rank canonical output matched. Nevertheless, the
parallel stage was slower and used about 14 MiB more peak RSS; process CPU rose
from 3.160 to 3.725 seconds. Its complete helper took 4.660 → 5.330 seconds.
An earlier one-input-to-four-input comparison conflated partitioning and source
changes; its apparent improvement is not an isolated GCSA-3B benefit. This
matched-input fixture does not justify recommending join concurrency for speed.
GCSA-4 used a 16 MiB resident allowance, then 256 KiB to force spill, and
exercised one million rolling advances at 1,024 live nodes and a
50,000-record forced spill with an independent record oracle. Its large benefit
belongs to this rolling-window workload, not whole GCSA construction.

GCSA-5A/5B used a 64 MiB sort-stage allowance and fan-in eight, 557,056 path
records, 1,114,112 ranks, and three sort runs. This allowance was passed directly
to the actual sorter; it is not a claim about a 64 MiB complete constructor. Framed
headers were observed, the canonical record digest matched, and all prefetch
candidate arms had an extra native task. The 5B sort stage was faster in each
cycle, at a roughly 14 MiB memory cost. A preceding complete-helper batch was
mixed: median 4.685 → 4.985 seconds and RSS 29.7 → 42.9 MiB. The later batch
with the stage timer was 4.305 → 4.145 seconds. Both are retained; they do not
establish a consistent complete-helper or whole-construction benefit.

GCSA-6 used a 256 MiB working allowance and consumed two million prepared path
events with edge, sampling, occurrence, and redundancy streams. Both arms
produced the same 2,951,075 serialized bytes
and SHA-256, and reloaded sample/counter checks passed. Candidate task count was
five versus baseline two, including the identical local sampler. The measured
stage excludes event generation and reload. Process CPU was 1.830 → 0.595
seconds. This does not attribute a complete-construction saving to the stage.

GCSA-8 timed verification alone against one canonical three-row cyclic index
constructed beforehand. The verifier input repeated those same three records
300,000 times, preserving their endpoint identities. Both sources received the
same 1 MiB allowance; the foundation verifier remains resident, while GCSA-8
uses bounded temporary sorting. Every arm queried exactly three distinct patterns,
emitted the complete-verification marker, reported no failed pattern, and left
scratch empty. Peak memory fell by about 27 MiB, while runtime rose about 37%.
Complete-process CPU was 0.960 → 1.140 seconds. This is a verification tradeoff,
not construction performance or an RSS-limit promise.

GCSA-LCP compared the same 16-path cyclic shard (4,352 rows, 3,805 query
patterns), four threads, FD 128, and a 512 MiB allowance on both sides.
Every candidate emitted the existing overlap marker and no serial-fallback
marker. All twelve ordinary GCSA/LCP pairs matched and passed reloaded queries.
The overlap result was about 3% slower at the median and used about 43 MiB
more peak RSS. Complete-process CPU was 10.220 → 10.920 seconds. This proves
activation and compatibility, but does not establish a complete-construction
speedup on this input.

## Excluded measurements and remaining gates

Failed pilots and incomplete runs were retained locally and excluded. These
include RNA inputs with invalid allele adjacency, GCSA inputs with incorrect
k-mer endpoints, the wide distance-query failure, and a whole-GCSA optional
comparison with mismatched O3/O2 owner flags, and the larger vg GCSA caller
stall. Corrected accepted runs use valid
inputs and matched flags. Historical results retain their recorded source and
input scope; this page does not promote them to measurements of a replay.

Supported-platform CI, sanitizer/platform coverage beyond recorded checks, and
representative real-input measurements remain separate gates. Lifetime-only
drafts retain low priority. R20 and GCSA-7 remain performance holds, D02 remains
a compatibility hold, and X5/P05 remain withdrawn. These measurements add no
production interface, resume/workspace contract, autoindex integration, mapped
pruning, RNA spooling, splice-search, transcript-body output, or parallel TSV.

## Timing variation

Ranges include all six observations per arm. Each cycle entry is the
baseline/candidate median of its two observations. Units are seconds;
stage rows use their internal timer, and command rows use process wall time.

| Comparison | Baseline range | Candidate range | Cycle 1 | Cycle 2 | Cycle 3 |
| --- | ---: | ---: | ---: | ---: | ---: |
| R03 | 16.540–20.090 | 6.440–10.720 | 18.505/10.395 | 18.315/7.405 | 18.715/10.250 |
| R07 | 2.820–3.460 | 2.810–3.280 | 3.180/3.085 | 2.915/2.840 | 2.855/2.845 |
| R08 mutation | 19.590–20.820 | 19.560–21.350 | 20.330/20.095 | 19.675/19.605 | 20.535/21.035 |
| R08 aligned | 7.390–8.600 | 8.270–8.710 | 7.900/8.525 | 8.045/8.690 | 8.255/8.320 |
| R09 | 1.300–1.470 | 1.280–1.470 | 1.360/1.320 | 1.450/1.455 | 1.430/1.460 |
| R10 | 2.960–3.780 | 3.110–3.580 | 3.580/3.255 | 3.160/3.565 | 3.655/3.300 |
| R11 | 16.140–20.960 | 18.090–19.270 | 20.050/18.605 | 17.830/18.755 | 19.460/18.205 |
| R16 | 11.150–12.720 | 8.700–10.480 | 11.585/8.875 | 11.325/9.480 | 11.935/10.000 |
| R17 | 7.210–8.310 | 5.240–7.380 | 7.620/6.980 | 7.975/6.000 | 7.240/5.665 |
| B2 combined | 2.150–2.180 | 1.690–1.950 | 2.150/1.920 | 2.165/1.915 | 2.150/1.820 |
| B1 four-worker insertion | 1.483–1.713 | 1.134–1.577 | 1.659/1.343 | 1.559/1.138 | 1.567/1.501 |
| B1 serial insertion | 1.488–1.593 | 0.955–1.213 | 1.540/1.107 | 1.526/1.008 | 1.535/1.007 |
| XG-vg combined | 22.890–28.820 | 7.290–7.870 | 26.770/7.560 | 25.215/7.865 | 27.060/7.415 |
| B2 same library, short | 1.920–2.120 | 2.050–2.420 | 1.980/2.320 | 2.055/2.250 | 1.960/2.085 |
| B2 same library, long | 6.010–6.570 | 6.530–6.690 | 6.035/6.550 | 6.270/6.650 | 6.320/6.615 |
| D01 sparse | 0.020–0.020 | 0.020–0.090 | 0.020/0.020 | 0.020/0.055 | 0.020/0.020 |
| D01 cyclic | 0.400–0.460 | 0.190–0.280 | 0.415/0.235 | 0.425/0.220 | 0.435/0.205 |
| GCSA-1 construction | 57.460–67.140 | 38.990–42.290 | 60.545/41.740 | 59.255/39.720 | 62.985/39.040 |
| GCSA-9 caller construction | 44.530–54.050 | 3.870–8.740 | 50.905/5.420 | 50.980/6.500 | 45.915/5.325 |
| GCSA-2 sort | 1.658–3.031 | 1.267–2.853 | 2.050/1.672 | 1.684/2.387 | 2.608/2.060 |
| GCSA-3A join | 8.837–13.564 | 7.131–11.236 | 11.774/7.874 | 11.514/9.411 | 10.177/10.522 |
| GCSA-3B same four shards | 1.387–3.160 | 2.691–4.254 | 2.273/4.096 | 2.158/2.840 | 2.813/3.134 |
| GCSA-8 verification | 0.930–1.050 | 1.200–1.500 | 0.965/1.255 | 1.000/1.350 | 0.985/1.390 |
| GCSA-LCP construction | 20.220–22.920 | 20.530–23.030 | 22.105/20.825 | 20.600/22.735 | 21.740/22.140 |
| GCSA-4 window/spill | 6.708–6.775 | 0.050–0.053 | 6.724/0.051 | 6.753/0.051 | 6.712/0.052 |
| GCSA-5A sort | 2.236–4.893 | 1.893–2.739 | 3.605/2.340 | 3.373/1.900 | 4.094/1.924 |
| GCSA-5B sort | 1.889–2.807 | 1.797–1.934 | 2.108/1.863 | 2.350/1.884 | 2.074/1.812 |
| GCSA-6 components | 1.729–1.773 | 0.201–0.252 | 1.757/0.226 | 1.756/0.211 | 1.743/0.227 |

## Exact source provenance

Source IDs below identify the code that was built. Public review aliases for vg
have identical source trees; optional GCSA aliases were rechecked and also have
identical trees. The GCSA foundation public alias adds documentation only.
The deliberately overridden GBWT pin in the B2 control is stated explicitly.

| Comparison | Baseline source | Candidate source | Selected dependency |
| --- | --- | --- | --- |
| R03 | `c91cc1d91fe1894274c6905c6bd089be12911329` | `f2055c87dfc042345fea20da00bc17802e0b6d4d` | stock pins |
| R07 | `f2055c87dfc042345fea20da00bc17802e0b6d4d` | `842c5eb9f6d7e6570a5ff422a1cf2db97c829f75` | stock pins |
| R08 | `c4b7dd0fd5e5b83604bd2847ce3008ddba40ff89` | `2d500831c2e6003daf8d07e6619c6abae2223c1e` | stock pins |
| R09 | `2d500831c2e6003daf8d07e6619c6abae2223c1e` | `d30edc55f5486b10ecacf42e4e1c8346445f9057` | stock pins |
| R10 | `2d500831c2e6003daf8d07e6619c6abae2223c1e` | `677e5487ee7c04f8b4bac918bdb96b8aa04683ca` | stock pins |
| R11 | `2d500831c2e6003daf8d07e6619c6abae2223c1e` | `df416859271c83c908da66870e18d749e253b719` | stock pins |
| R16 | `c91cc1d91fe1894274c6905c6bd089be12911329` | `56c32ac6a054be485b97a1a3102a746e8dd69af5` | libbdsg 3d45611e917cf341fee1416f031bb9196637cc88 |
| R17 | `56c32ac6a054be485b97a1a3102a746e8dd69af5` | `b8142e6526ae454d74ae76f5e9ea7b996931351c` | libbdsg 531c1ffbfd1998eb18aedf0ac9c5091c77027c98 |
| XG-vg | `c91cc1d91fe1894274c6905c6bd089be12911329` | `bb60d269871da0c5041d0e87b797087422ed462c` | XG 152446f7835d9ca37ec6144a51fd8494d9f3f3fe |
| B1 immediate parent | `14a06d588ea6de50cd809f546b9bb57c92c15ac1` | `6cabe5fc2cef04d922d68b561d190b0c7ee02bd8` | Matching owner headers/archives |
| B2 same-library control | vg `c91cc1d91fe1894274c6905c6bd089be12911329` | vg `677933f730a0a0665a17dc1078b4e2896a2fd0fe` | Both GBWT `6cabe5fc2cef04d922d68b561d190b0c7ee02bd8` |
| D01-vg | vg `bea9cacb3370f1e10194b0342720ee8bc7163430`, libbdsg `b6d327da7e3447a38f99b51c54548934a7e30981` | vg `3f520622bf753933e380ea33bdec33848ff8b78a`, libbdsg `289bf176ecf3f52859422ab85f9e2d045741d8bb` | Actual snarl construction and reloaded queries |
| GCSA-1 | `3479b0811262f4b606abae7ebd168510f5ee793d` | Same executable | Resident versus 16 MiB external route |
| GCSA-2 | `3479b0811262f4b606abae7ebd168510f5ee793d` | `ec6b6ae069f7cf64ff9215b7d55ff18cdc748ae4` | Matching C++17/O2 archives |
| GCSA-5A | `3479b0811262f4b606abae7ebd168510f5ee793d` | `22268ed4a6d589dfacbfee9c1cbfcd3453f8df6d` | Matching C++17/O2 archives |
| GCSA-5B | `22268ed4a6d589dfacbfee9c1cbfcd3453f8df6d` | `cdfcd6cac69c5e64a5d2b73886e6fbd69d5670c3` | Matching C++17/O2 archives |
| GCSA-8 | `3479b0811262f4b606abae7ebd168510f5ee793d` | `c462c0f6d4b15ac6c02525f046cbd4199d27ccc0` | Matching C++17/O2 archives |
| GCSA-3A | `3479b0811262f4b606abae7ebd168510f5ee793d` | `4b3f3f13ac3f5fa48b5a0686fa4698101ad3c331` | Matching C++17/O2 archives |
| GCSA-3B | `4b3f3f13ac3f5fa48b5a0686fa4698101ad3c331` | `5d8060784f73f6dbdc24af3757f678dc420b7390` | Matching C++17/O2 archives |
| GCSA-6 | `5d8060784f73f6dbdc24af3757f678dc420b7390` | `17b5d5aa13bd55a61c218171903b83ad1eae7636` | Matching C++17/O2 archives |
| GCSA-4 | `3479b0811262f4b606abae7ebd168510f5ee793d` | `9fd741c5068e24256668a0b034b0aa7392255d12` | Matching C++17/O2 archives |
| GCSA-LCP | `17b5d5aa13bd55a61c218171903b83ad1eae7636` | `1b476839c5baa9a63ba5a6c7cbe6c77093efa552` | Matching C++17/O2 archives |
| GCSA-9 vg caller | vg `533290bc2a1c524b1b899b5a2c78f40f59eb44c8` | Same executable | GCSA `3479b0811262f4b606abae7ebd168510f5ee793d` |

All vg pairs used SDSL `993857a820887787ddaf0ab678eca2d3ab30b741`
and libhandlegraph `07fd981f0f43e9d4d8aa61a84169a9eeda69152a`.
Stock pins were GCSA `1cb4a580e1a5983c08c99c370afb166a686efe20`,
libbdsg `a2e873b80b10b38b90f1f82bee3044116b6276fc`,
GBWT `625bd09ffadd8a633524b60e1af7c7b6eea65d0c`,
and XG `b306222cbe4bc14321c90a390e2f26ac69ab1cb8`.
Only the changes shown for each pair are overridden.
