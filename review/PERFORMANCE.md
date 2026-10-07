# Performance evidence

This page connects the [proposal drafts](https://github.com/JosephLalli/vg/issues/11)
to their measurements. This page preserves the earlier narrow stage receipts.
[Current command and implementation-stage measurements](COMMAND-PERFORMANCE.md)
adds the 2026-10-07 controlled comparisons, including negative results. New
measurement helpers, inputs and raw receipts remain local.

## Current small-fixture measurements

The comparisons below used three ABBA cycles, with six observations per arm.
Times and process peak RSS are medians. Representation times are GNU `time`
process wall times, rounded to hundredths of a second in the receipt; the probe's
more precise internal timers are also preserved there. Serialization and distance
times use internal stage timers. Prune times include the complete command.

| Proposal | Input and measured work | Baseline → candidate time | Baseline → candidate peak RSS | Correctness and raw receipt |
| --- | --- | --- | --- | --- |
| R03: compact edited mappings | 500,000 records; representation construction and traversal in a standalone process | 0.13 → 0.01 s | 110 → 14 MiB | Equal value digests; [representation probes](evidence/representation-probes.json) |
| R08: shared immutable sources | 10,000 paths of 1,000 steps; expanded versus shared representations in a standalone process | 0.17 → 0.01 s | 230 → 2 MiB | Equal value digests; [representation probes](evidence/representation-probes.json) |
| R17 versus R16: parallel output | 100 paths of 20,000 steps over 512 nodes, four workers; final graph output only | 0.507146 → 0.215868 s | 38 → 38 MiB | All twelve ordinary graph files byte-identical; [serialization](evidence/serialization-current.json) |
| P01–P03: ordinary prune | 8,000 synthetic components, four workers; graph load, prune/unfold and output | 3.250 → 1.065 s | 52.7 → 54.7 MiB | Equal graph manifests and exact mapping bytes; [prune](evidence/prune-current.json) |
| D01: dense distance staging | 1,000 nodes, 1,998,000 distance records; allocation and filling only | 0.271540 → 0.022613 s | 145.5 → 12 MiB | Equal staging input digests; separate 1,328 serialized/reloaded query comparisons; [distance](evidence/distance-current.json) |
| D01: sparse distance staging | Same node domain, 15,584 selected distance records; allocation and filling only | 0.006548 → 0.005401 s | 12 → 12 MiB | Same independent query comparison; [distance](evidence/distance-current.json) |

The output-stage median is 57.4% lower; the synthetic whole-prune-command median
is 67.2% lower. These are changes in the measured fixtures. Neither percentage
predicts whole-genome runtime. The very short representation and sparse-distance
probes do not support precise runtime extrapolation.

Serialization setup is excluded from its stage timer but included in process
peak RSS. Prune fixture/index preparation is excluded from its command timer.
Distance staging is measured separately from complete index construction; its
input digest is not the query-equivalence test. R03/R08 are comparisons between
representations compiled into probes, rather than paired whole-`vg rna` commands.
Calling R08's legacy vector getter materializes owned paths, so its isolated
shared-storage memory result does not describe every API consumer.

## Source and environment

The measured source revisions are listed below. Some personal-fork drafts preserve
review history through different commit IDs with identical source trees. The
published heads were read back and those tree equalities rechecked on 2026-10-07.
[Public source trees](evidence/public-source-trees.json) and
[current builds](evidence/current-vg-builds.json) record full revisions, trees and
dependency pins; abbreviated revisions below are navigation aids.

| Measurement | Source used by the probe or command | Published source relationship |
| --- | --- | --- |
| R03 representations | Both representations compiled against `f2055c87dfc0` | Same commit as [R03](https://github.com/JosephLalli/vg/pull/27) |
| R08 representations | Both representations compiled against `2d500831c2e6` | Same tree as [R08](https://github.com/JosephLalli/vg/pull/30) head `2f7df3ee7f9d` |
| Serial → parallel output | R16 `56c32ac6a054` → R17 `b8142e6526ae`; libbdsg `3d45611e917c` → `531c1ffbfd19` | Same trees as [R16](https://github.com/JosephLalli/vg/pull/34) `d617a2b3f1dd` and [R17](https://github.com/JosephLalli/vg/pull/36) `9d7ca417c8ed` |
| Whole prune command | Baseline `bea9cacb3370` → candidate `a36417eef87b`; libbdsg `b6d327da7e34` → `22b07024fbbe` | Candidate is the [P01–P03](https://github.com/JosephLalli/vg/pull/20) head |
| Distance staging and query checks | Staging probe uses libbdsg `289bf176ecf3`; query comparators use vg `bea9cacb3370` → `3f520622bf75` | Library is [D01](https://github.com/JosephLalli/libbdsg/pull/5); candidate vg tree equals [D01-vg](https://github.com/JosephLalli/vg/pull/35) head `dab5ac78c2c1` |

The environment was Ubuntu 22.04 with GCC 11.4. The isolated review container had
a 24 GiB memory limit and CPUs 232–247; command fixtures had a 2 GiB address-space
limit. Review builds had stopped during timing, while the shared host remained
otherwise uncontrolled. ABBA balances order effects; it does not eliminate other
users' background load. [Provenance](evidence/provenance.json) records binary and
probe hashes, environment and validation summaries. Process peak RSS and an
algorithm's extra-workspace allowance are different quantities.

## Coverage beyond the earlier narrow probes

The [current measurement page](COMMAND-PERFORMANCE.md) supplies selected exact-
source fixture comparisons for complete RNA commands, the GBWT insertion library
and caller, combined XG construction, complete snarl-distance construction,
GCSA resident/external construction and its optional implementation stages.
Ranges, source revisions, input sizes, thread settings and acceptance gates are
reported there. A completed comparison may establish a tradeoff or regression;
it does not necessarily establish a benefit.

The matched B2 caller and matched-shard GCSA-3B comparisons regressed on their
inputs. LCP overlap executed without establishing a construction speedup.
R08 has route-dependent memory costs, and GCSA-5B's complete-helper batches were
mixed despite a faster isolated sort stage. These findings qualify the proposal
claims; earlier broader or historical results are not substituted for them.

Supported-platform CI and representative real-input evaluation remain separate
gates. Lifetime-only drafts keep their low priority. R20 and GCSA-7 remain
performance holds; D02 remains held on input compatibility, and X5/P05 remain
withdrawn. No measurement reopens the excluded development or production scope.

## Reproduce or extend the evidence

The [review-kit instructions](README.md) give build and fixture invocations.
Raw receipts contain the exact commands and each arm's result. Use matching
headers, archives and pins for each recorded source; local receipt paths identify
the original environment and must be replaced in another checkout.

A representative whole-command claim requires independently built matching
baseline/candidate executables, identical inputs and thread settings, repeated
balanced timing with peak RSS, and output-equivalence checks. Record graph, path,
transcript and step counts as appropriate. Review host resources before a run and
keep the existing serial-byte and threaded semantic comparison gates. These
measurements and local builds supplement supported-platform CI.
