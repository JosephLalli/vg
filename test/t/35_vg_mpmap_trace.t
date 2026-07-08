#!/usr/bin/env bash

BASH_TAP_ROOT=../deps/bash-tap
. ../deps/bash-tap/bash-tap-bootstrap

PATH=../bin:$PATH # for vg

plan tests 28

# Build a tiny graph + GCSA index (reuse the small xy fixtures used by 33_vg_mpmap.t).
vg construct -m 1000 -a -r small/xy.fa -v small/xy2.vcf.gz >tr.vg
vg index -x tr.xg -g tr.gcsa -k 16 tr.vg

# --- trace mode is disabled by default (no file written) --------------------
rm -f trace.jsonl
vg mpmap -B -P 1 -n rna -x tr.xg -g tr.gcsa -f reads/xy2.match.fq -F GAM >/dev/null 2>&1
is "$(test -e trace.jsonl && echo present || echo absent)" "absent" "no trace file is written when --trace-splice-search is not given"

# --- trace mode writes a JSONL file -----------------------------------------
vg mpmap -B -P 1 -n rna -x tr.xg -g tr.gcsa -f reads/xy2.match.fq --trace-splice-search trace.jsonl -F GAM >tr.out.gam 2>/dev/null
is "$?" "0" "vg mpmap with --trace-splice-search exits successfully"
is "$(test -s trace.jsonl && echo yes)" "yes" "trace file is created and non-empty"

# --- the file is self-describing: first line is a header record -------------
is "$(head -n1 trace.jsonl | jq -r '.record_type')" "header" "first line is a header record"
is "$(head -n1 trace.jsonl | jq -r '.schema')" "mpmap_splice_search_trace" "header names the trace schema"

# --- read records carry the required metric fields --------------------------
REC=$(grep '"record_type":"read"' trace.jsonl | head -n1)
is "$(echo "$REC" | jq 'has("read_name") and has("n_mems_total") and has("n_mem_hits_total") and has("n_mems_by_length_bin")')" "true" "read record has seed/MEM visibility fields"
is "$(echo "$REC" | jq 'has("n_clusters") and has("n_cluster_graphs") and has("best_score") and has("n_alt_mappings_after_cap")')" "true" "read record has cluster/alignment fields"
is "$(echo "$REC" | jq 'has("n_proactive_intervals") and has("anchors") and has("time_total_usec") and has("elapsed_usec_total")')" "true" "read record has proactive-proxy, anchor and timing fields"

rm -f tr.vg tr.xg tr.gcsa tr.gcsa.lcp tr.out.gam trace.jsonl

# --- RNA splice fixture: exercises the deep splice/candidate/gate/timer metrics --
# Reuse the tiny spliceable graph from 33_vg_mpmap.t (needs a distance index).
vg construct -m 32 -r small/x.fa -v small/x.vcf.gz >trs.vg
vg index trs.vg -x trs.xg -g trs.gcsa
vg index trs.vg -j trs.dist

# single-end read whose optimal alignment requires a spliced route
printf '@sread\nCAAATAAGGCTTGGAAATTTTCTGGAGTTCTATTATATTCCAACTCTCTGGCCATTTTAAGTTTCCTGTGGACTAAGGACAAAGGTGCGGGGAGATGA\n+\nHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH\n' >trs.fq
rm -f trs.jsonl
vg mpmap -x trs.xg -d trs.dist -g trs.gcsa -B -n rna -f trs.fq --trace-splice-search trs.jsonl -t 1 >trs.gamp 2>/dev/null
SREC=$(grep '"record_type":"read"' trs.jsonl | head -n1)

is "$(echo "$SREC" | jq '.do_spliced_alignment and .splice_changed_primary')" "true" "splice rescue fires and changes the primary on a spliced read"
is "$(echo "$SREC" | jq '.min_softclip_length_for_splice > 0 and .max_softclip_overlap > 0 and has("min_softclipped_score_for_splice")')" "true" "record echoes the actual soft-clip gate thresholds"
is "$(echo "$SREC" | jq '[.anchors[].n_candidates_aligned] | add >= 1')" "true" "at least one anchor had a splice candidate that aligned"
is "$(echo "$SREC" | jq '.anchors[0] | has("n_candidates_rejected_before_alignment") and has("n_candidates_rejected_after_alignment")')" "true" "anchors carry before/after-alignment rejection counts"

# paired reads (explicit fragment length via -I/-D triggers full paired mode)
printf '@pr1\nCAAATAAGGCTTGGAAATTTTCTGGAGTTCTATTATATTCCAACTCTCTGGTTCCTGGTGCTATGTGTAACTAG\n+\nHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH\n@pr2\nTCATCTCCCCGCACCTTTGTCCTTAGTCCACAGGAAACTCTGCTGTCAGTAGTATCATCTCCATATTAGAGATA\n+\nHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH\n' >trs.pe.fq
rm -f trs.pe.jsonl
vg mpmap -x trs.xg -d trs.dist -g trs.gcsa -B -n rna -f trs.pe.fq -i -I 350 -D 10 --trace-splice-search trs.pe.jsonl -t 1 >trs.pe.gamp 2>/dev/null

is "$(grep '"record_type":"read"' trs.pe.jsonl | jq -s '[.[].mate] | (index(1) != null) and (index(2) != null)')" "true" "paired mapping emits one record per mate (mate 1 and mate 2)"
is "$(grep '"record_type":"read"' trs.pe.jsonl | jq -s 'all(.[]; .is_paired)')" "true" "paired records are flagged is_paired"
is "$(grep '"record_type":"read"' trs.pe.jsonl | jq -s 'all(.[]; (.time_find_mems_usec + .time_cluster_usec + .time_query_cluster_graphs_usec) > 0)')" "true" "paired records carry non-zero pre-clustering phase timers"
is "$(grep '"record_type":"read"' trs.pe.jsonl | jq -s 'all(.[]; has("do_spliced_alignment"))')" "true" "paired records carry the splice-search outcome fields"

# --- experimental STAR-style MMP seed generator (--mmp-seed): schema v2 + fields + isolation --
rm -f trs.mmp.jsonl trs.mmp.gamp
vg mpmap -x trs.xg -d trs.dist -g trs.gcsa -B -n rna -f trs.fq --mmp-seed --trace-splice-search trs.mmp.jsonl -t 1 >trs.mmp.gamp 2>/dev/null
MREC=$(grep '"record_type":"read"' trs.mmp.jsonl | head -n1)

is "$(head -n1 trs.mmp.jsonl | jq -r '.schema_version')" "2" "trace schema is version 2 (adds MMP seed fields)"
is "$(echo "$MREC" | jq 'has("n_mmp_seeds_generated") and has("n_mmp_seeds_new") and has("n_mmp_seeds_kept_short") and has("time_mmp_seed_usec")')" "true" "read record carries the MMP seed metrics with --mmp-seed"
is "$(echo "$SREC" | jq '.n_mmp_seeds_generated == 0 and .n_mmp_seed_hits == 0')" "true" "MMP counters are zero when --mmp-seed is not given (default-off isolation)"
is "$(echo "$MREC" | jq '.do_spliced_alignment')" "true" "spliced alignment still runs with --mmp-seed enabled"

# --- C (RC right tail), A (sequential chaining), B (configurable motif scoring) ---
is "$(echo "$MREC" | jq 'has("n_mmp_rc_seeds") and has("n_mmp_chain_seeds")')" "true" "read record carries RC and chain seed metrics"

vg mpmap -x trs.xg -d trs.dist -g trs.gcsa -B -n rna -f trs.fq --mmp-seed --mmp-strand-mode both -t 1 >trs.both.gamp 2>/dev/null
is "$?" "0" "vg mpmap runs with --mmp-strand-mode both (reverse-complement right-tail path)"

vg mpmap -x trs.xg -d trs.dist -g trs.gcsa -B -n rna -f trs.fq --mmp-seed --mmp-chain --mmp-max-seeds 4 -t 1 >trs.chain.gamp 2>/dev/null
is "$?" "0" "vg mpmap runs with --mmp-chain (sequential MMP chaining)"

printf 'GT AG 0.9924\nGC AG 0.0069\nAT AC 0.0005\n' >trs.motifs.tsv
vg mpmap -x trs.xg -d trs.dist -g trs.gcsa -B -n rna -f trs.fq --splice-motif-scores trs.motifs.tsv --trace-splice-search trs.motif.jsonl -t 1 >trs.motif.gamp 2>/dev/null
is "$?" "0" "vg mpmap runs with --splice-motif-scores (configurable splice-motif table)"
is "$(grep '"record_type":"read"' trs.motif.jsonl | head -n1 | jq '.do_spliced_alignment')" "true" "spliced alignment runs with a custom splice-motif table"

# --- MMP as a whole-read seeding source: --mmp-primary (replace MEMs) / --mmp-augment (MEM + MMP) --
vg mpmap -x trs.xg -d trs.dist -g trs.gcsa -B -n rna -f trs.fq --mmp-primary --trace-splice-search trs.prim.jsonl -t 1 >trs.prim.gamp 2>/dev/null
is "$?" "0" "vg mpmap runs with --mmp-primary (pure MMP seeding, MEM pool replaced)"
is "$(grep '"record_type":"read"' trs.prim.jsonl | head -n1 | jq 'has("n_mmp_primary_seeds")')" "true" "read record carries the primary-seed metric"

vg mpmap -x trs.xg -d trs.dist -g trs.gcsa -B -n rna -f trs.fq --mmp-augment -t 1 >trs.aug.gamp 2>/dev/null
is "$?" "0" "vg mpmap runs with --mmp-augment (MEM pool augmented with MMP seeds)"

rm -f trs.vg trs.xg trs.gcsa trs.gcsa.lcp trs.dist trs.fq trs.gamp trs.jsonl trs.pe.fq trs.pe.gamp trs.pe.jsonl trs.mmp.jsonl trs.mmp.gamp trs.both.gamp trs.chain.gamp trs.motifs.tsv trs.motif.jsonl trs.motif.gamp trs.prim.jsonl trs.prim.gamp trs.aug.gamp
