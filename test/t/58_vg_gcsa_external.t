#!/usr/bin/env bash

BASH_TAP_ROOT=../deps/bash-tap
. ../deps/bash-tap/bash-tap-bootstrap

PATH=../bin:$PATH

plan tests 23

# Keep autoindex's external-memory controls wired through the parent vg source
# without requiring a relink of the binary used by the integration checks below.
grep -Fq 'params.setWorkerExecutable(argv[0]);' ../src/subcommand/index_main.cpp
is $? 0 "index passes argv[0] to external join workers"
grep -Fq 'IndexingParameters::gcsa_worker_executable = argv[0];' ../src/subcommand/autoindex_main.cpp
is $? 0 "autoindex records argv[0] for external join workers"
grep -Fq '{"gcsa-sort-run-size", required_argument, 0, OPT_GCSA_SORT_RUN_SIZE}' ../src/subcommand/autoindex_main.cpp
is $? 0 "autoindex parses the sort-run workspace override"
grep -Fq '{"gcsa-join-partition-size", required_argument, 0, OPT_GCSA_JOIN_PARTITION_SIZE}' ../src/subcommand/autoindex_main.cpp
is $? 0 "autoindex parses the join-partition workspace override"
grep -Fq 'IndexingParameters::gcsa_sort_run_size = gcsa::parseBytes(optarg);' ../src/subcommand/autoindex_main.cpp
is $? 0 "autoindex propagates the sort-run workspace override"
grep -Fq 'IndexingParameters::gcsa_join_partition_size = gcsa::parseBytes(optarg);' ../src/subcommand/autoindex_main.cpp
is $? 0 "autoindex propagates the join-partition workspace override"
grep -Fq 'params.setSortRunSize(IndexingParameters::gcsa_sort_run_size);' ../src/index_registry.cpp
is $? 0 "index registry forwards the sort-run workspace override"
grep -Fq 'params.setJoinPartitionSize(IndexingParameters::gcsa_join_partition_size);' ../src/index_registry.cpp
is $? 0 "index registry forwards the join-partition workspace override"
grep -Fq 'aggregate external-construction working-set target' ../src/subcommand/index_main.cpp
is $? 0 "index help describes memory as an aggregate operational target"
grep -Fq 'gcsa::LCPArray::buildAndStore(input_graph, params, gcsa_name + ".lcp");' ../src/subcommand/index_main.cpp
is $? 0 "index streams external LCP directly to its final file"
grep -Fq 'gcsa::LCPArray::buildAndStore(input_graph, params, lcp_output_name);' ../src/index_registry.cpp
is $? 0 "index registry streams external LCP directly to its final file"

rm -rf gcsa-external-work legacy.gcsa* external.gcsa* resumed.gcsa* \
    refused.gcsa* changed.gcsa* corrupt.gcsa* x.vg y.vg

# Two named graph files are important here: they are two semantic GCSA2 input
# graphs. Each may spill to many workspace artifacts without changing that
# logical identity or SameFromFile pruning behavior.
vg construct -r small/xy.fa -v small/xy2.vcf.gz -R x -C > x.vg 2>/dev/null
vg construct -r small/xy.fa -v small/xy2.vcf.gz -R y -C > y.vg 2>/dev/null
vg ids -j x.vg y.vg

vg index -g legacy.gcsa -k 2 -X 2 -V x.vg y.vg >/dev/null 2>&1
is $? 0 "legacy GCSA2 construction succeeds on two logical inputs"

vg index -g external.gcsa -k 2 -X 2 -V \
    --gcsa-work-dir gcsa-external-work \
    --gcsa-memory-limit 1M --gcsa-disk-limit 1G \
    --gcsa-sort-run-size 1M --gcsa-join-partition-size 1M \
    --gcsa-process-workers 2 \
    x.vg y.vg >/dev/null 2>&1
is $? 0 "disk-first GCSA2 construction succeeds through fork-free workers with a tiny byte budget"

cmp legacy.gcsa external.gcsa
is $? 0 "disk-first GCSA is byte-identical to the legacy index"
cmp legacy.gcsa.lcp external.gcsa.lcp
is $? 0 "disk-first LCP is byte-identical to the legacy index"

test -s gcsa-external-work/inputs/kmers.manifest
is $? 0 "vg commits a durable semantic-input manifest"
test -s gcsa-external-work/build.json
is $? 0 "GCSA2 commits a durable construction manifest"

# Operational settings may change across resume. The durable input files and
# completed prefix-doubling checkpoints are validated and reused.
: > gcsa-external-work/inputs/orphan.partial
vg index -g resumed.gcsa -k 2 -X 2 -V \
    --gcsa-work-dir gcsa-external-work --gcsa-resume \
    --gcsa-memory-limit 768K --gcsa-disk-limit 1G \
    --gcsa-sort-run-size 768K --gcsa-join-partition-size 768K \
    x.vg y.vg >/dev/null 2>&1
is $? 0 "construction resumes with different operational memory and run budgets"
cmp legacy.gcsa resumed.gcsa && cmp legacy.gcsa.lcp resumed.gcsa.lcp
is $? 0 "resumed indexes are byte-identical to legacy construction"
test ! -e gcsa-external-work/inputs/orphan.partial
is $? 0 "resume removes an orphan partial input artifact"

vg index -g refused.gcsa -k 2 -X 2 \
    --gcsa-work-dir gcsa-external-work \
    --gcsa-memory-limit 1M --gcsa-disk-limit 1G \
    x.vg y.vg >/dev/null 2>&1
isnt $? 0 "an existing workspace requires an explicit resume request"

vg index -g changed.gcsa -k 2 -X 1 \
    --gcsa-work-dir gcsa-external-work --gcsa-resume \
    --gcsa-memory-limit 1M --gcsa-disk-limit 1G \
    x.vg y.vg >/dev/null 2>&1
isnt $? 0 "resume refuses a changed semantic doubling parameter"

truncate -s -1 gcsa-external-work/inputs/kmer-000000.graph
vg index -g corrupt.gcsa -k 2 -X 2 \
    --gcsa-work-dir gcsa-external-work --gcsa-resume \
    --gcsa-memory-limit 1M --gcsa-disk-limit 1G \
    x.vg y.vg >/dev/null 2>&1
isnt $? 0 "resume refuses a truncated durable k-mer artifact"

rm -rf gcsa-external-work legacy.gcsa* external.gcsa* resumed.gcsa* \
    refused.gcsa* changed.gcsa* corrupt.gcsa* x.vg y.vg
