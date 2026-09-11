#!/usr/bin/env bash
# Validation protocol for the vg rna memory patches (README.md section 6).
#
#   chrom_validation.sh CONTROL_VG PATCHED_VG OUTDIR
#
# CHROM (default chrY) selects the chromosome; the chunk and annotation are resolved from
# the canonical chunk set and whichever of the two annotation runs holds that chromosome.
#
# SKIP_T1=1 skips steps 0 and 1 (the -t 1 reproducibility floor and byte gate) and runs only
# the relabel-invariant digest gate, the RSS comparison, the unit test and validate.  The -t 1
# runs are single-threaded: on chrY they cost about a minute, but on a full-size chromosome
# they cost hours, and -t 1 byte-reproducibility is a property of vg rna already established
# on chrY.  Use SKIP_T1=1 when the question is scale (does the saving hold on a real
# chromosome) rather than byte-identity.  The REPORT records which mode was used.
#
# CONTROL_VG is the pinned production binary (sha256 4e63c323...), PATCHED_VG a binary
# built from a separate worktree with the patches applied.  OUTDIR must be a NEW directory
# on /mnt/ssd and must not be inside the hprc_v2_vg_rna production tree.
#
# Recipe: the production driver's exact vg rna invocation (build_sharded_nohg002_k30_index.sh,
# build_chromosome), which writes the transcript-path GBWT with -b.  The earlier -v recipe is
# not used: under -j the -v GBWT is the input haplotype index written back in the INPUT id
# space (the defect that crashed every whole-genome attempt), so gating it gates nothing the
# pipeline consumes.  -f is added only to obtain a sequence digest; it changes no other output.
#
# Order: step 0 (control vs control at -t 1) must pass before anything else is meaningful,
# so the script stops there on a mismatch.  Every vg call runs under /usr/bin/time -v with
# its own time file.  Exit status is 0 only if every gate passed.
set -uo pipefail

CONTROL=${1:?control vg binary}
PATCHED=${2:?patched vg binary}
OUT=${3:?output directory on /mnt/ssd}

CHROM=${CHROM:-chrY}
SKIP_T1=${SKIP_T1:-0}
THREADS=${THREADS:-32}
HPRC=/mnt/ssd/lalli/hprc_v2_vg_rna
CHUNKS=$HPRC/graph/hprc-v2.1-mc-chm13.full.noHG002.canonical_chunks_v1_20260802T112539-0500/chunks
A1=$HPRC/annotation_runs/chr20_chr21_chr22_transcript_dedup_then_correction_v1_20260822T210522-0500/chromosomes
A2=$HPRC/annotation_runs/remaining_chr1_19_X_Y_transcript_dedup_then_correction_v1_20260823T042657-0500/chromosomes
GBZ=$(ls "$CHUNKS"/*_"$CHROM".gbz 2>/dev/null | head -1)
if [[ -d $A1/$CHROM ]]; then ANN=$A1; else ANN=$A2; fi
GFF_GZ=$ANN/$CHROM/final/retained_exons_and_bodies.gff3.gz

die() { echo "ERROR: $*" >&2; exit 2; }
case "$OUT" in
  /mnt/ssd/lalli/hprc_v2_vg_rna/*) die "OUTDIR must not be inside the production tree: $OUT" ;;
  /mnt/ssd/*) ;;
  *) die "OUTDIR must be under /mnt/ssd (the default TMPDIR is a slow HDD)" ;;
esac
if [[ -e "$OUT" ]]; then
  [[ -d "$OUT" ]] || die "OUTDIR exists and is not a directory: $OUT"
  [[ -z "$(ls -A "$OUT")" ]] || die "OUTDIR exists and is not empty; use a fresh directory: $OUT"
fi
for f in "$CONTROL" "$PATCHED" "$GBZ" "$GFF_GZ"; do [[ -r "$f" ]] || die "missing: $f"; done
[[ -n "$GBZ" ]] || die "no chunk gbz for $CHROM under $CHUNKS"
[[ -x "$CONTROL" && -x "$PATCHED" ]] || die "binaries must be executable"

mkdir -p "$OUT/tmp" "$OUT/time"
export TMPDIR="$OUT/tmp"
GFF="$OUT/$CHROM.gff3"
gzip -dc "$GFF_GZ" > "$GFF"
sha256sum "$CONTROL" "$PATCHED" "$GBZ" "$GFF_GZ" > "$OUT/inputs.sha256"
{ echo "control: $("$CONTROL" version 2>/dev/null | head -1)"; echo "patched: $("$PATCHED" version 2>/dev/null | head -1)"; } > "$OUT/versions.txt"

FAIL=0
gate() { # gate <ok?> <text>
  if [[ $1 == 0 ]]; then echo "PASS       $2"; else echo "FAIL       $2"; FAIL=1; fi
}
# timed <label> <cmd...>: every vg invocation gets its own /usr/bin/time -v file.
timed() { local label=$1; shift; /usr/bin/time -v -o "$OUT/time/$label.time.txt" "$@"; }
rss_kib() { awk '/Maximum resident/{print $NF}' "$OUT/time/$1.time.txt"; }
wall()    { awk -F': ' '/Elapsed \(wall/{print $NF}' "$OUT/time/$1.time.txt"; }

# run <label> <binary> <threads>: the production recipe plus -f.
run() {
  local label=$1 bin=$2 threads=$3 rc
  timed "$label" "$bin" rna -t "$threads" -p -z -j -c no -s Parent -y exon -r \
    -b "$OUT/$label.prune_paths.gbwt" -i "$OUT/$label.info.tsv" -f "$OUT/$label.tx.fa" \
    -n "$GFF" "$GBZ" > "$OUT/$label.pg" 2> "$OUT/$label.stderr"
  rc=$?
  echo "run        $label exit=$rc maxrss_kib=$(rss_kib "$label") wall=$(wall "$label")"
  [[ $rc == 0 ]] || { echo "ERROR: $label failed (exit $rc); see $OUT/$label.stderr" >&2; exit 3; }
}

# Digests that survive non-reproducible node numbering (vg rna at -t>1 is not
# byte-reproducible for .pg and GBWT; commit 0d6a46873 and WORKSPACE_STATE.md section 5):
#   fasta_digest: wrap-agnostic.  write_fasta_sequence wraps at 80 columns, so a per-line
#     pairing (the old `paste - -`) breaks on any record longer than 80 bp and can report a
#     false DIFFERENT.  Accumulate sequence lines per header, key on the first header token,
#     sort by name.  Names are assigned after a by-name sort, so they are relabel-invariant.
#   gbwt_digest: the -b GBWT rendered as GAF by `vg paths -A`; per path keep name, query
#     length (col 2) and step count (number of oriented steps in col 6).  Node ids are
#     excluded on purpose: they change with thread count.  Names come from the annotation,
#     length is sequence length, and the step count depends only on WHICH exon boundaries
#     divide which nodes -- the same set regardless of the order the divisions happened in.
#   node_seq_digest: the multiset of node sequences from the GFA S lines.
fasta_digest()    { awk 'BEGIN{OFS="\t"} /^>/{if(n!="")print n,s; n=substr($1,2); s=""; next} {s=s $0} END{if(n!="")print n,s}' "$1" | sort -k1,1 | sha256sum | cut -d' ' -f1; }
gbwt_digest()     { # <label> <binary>
  timed "$1.paths" "$2" paths -x "$OUT/$1.pg" -g "$OUT/$1.prune_paths.gbwt" -A 2>"$OUT/$1.paths.stderr" \
    | awk -F'\t' '!/^@/ {n=gsub(/[<>]/,"&",$6); print $1"\t"$2"\t"n}' | sort -k1,1 | sha256sum | cut -d' ' -f1; }
node_seq_digest() { timed "$1.convert" "$2" convert -f "$OUT/$1.pg" 2>"$OUT/$1.convert.stderr" | awk '$1=="S"{print $3}' | sort | sha256sum | cut -d' ' -f1; }

if [[ $SKIP_T1 == 1 ]]; then
  echo "# SKIP_T1=1: steps 0 and 1 (-t 1 floor and byte gate) not run for $CHROM."
  echo "#   Byte-identity at -t 1 was established on chrY; this run measures scale instead."
else
echo "# step 0: reproducibility floor -- control vs control at -t 1; all four must be byte-identical"
run control_t1_a "$CONTROL" 1
run control_t1_b "$CONTROL" 1
floor_ok=0
for ext in pg prune_paths.gbwt info.tsv tx.fa; do
  if cmp -s "$OUT/control_t1_a.$ext" "$OUT/control_t1_b.$ext"; then echo "identical  $ext"; else echo "DIFFERENT  $ext"; floor_ok=1; fi
done
if [[ $floor_ok != 0 ]]; then
  echo "STOP: vg rna is not byte-reproducible at -t 1 with the control binary, so a byte gate cannot" | tee "$OUT/REPORT.txt"
  echo "      be applied to the patched binary.  Only the digest gate (step 2) could be used; rerun" | tee -a "$OUT/REPORT.txt"
  echo "      with that understanding after deciding it is acceptable.  Outputs kept in $OUT."       | tee -a "$OUT/REPORT.txt"
  exit 4
fi
fi

{
  echo "# chromosome: $CHROM   threads: $THREADS   skip_t1: $SKIP_T1"
  if [[ $SKIP_T1 == 1 ]]; then
    echo "# steps 0-1 (-t 1 floor and byte gate) SKIPPED; byte-identity established on chrY"
  else
  echo "# step 1: byte gate -- patched vs control at -t 1"
  run patched_t1 "$PATCHED" 1
  for ext in pg prune_paths.gbwt info.tsv tx.fa; do
    if cmp -s "$OUT/control_t1_a.$ext" "$OUT/patched_t1.$ext"; then gate 0 "byte-identical $ext"; else gate 1 "byte-identical $ext"; fi
  done
  fi

  echo "# step 2: digest gate at -t 32 (relabel-invariant)"
  run control_t32 "$CONTROL" "$THREADS"
  run patched_t32 "$PATCHED" "$THREADS"
  a=$(sha256sum < "$OUT/control_t32.info.tsv" | cut -d' ' -f1); b=$(sha256sum < "$OUT/patched_t32.info.tsv" | cut -d' ' -f1)
  echo "info.tsv      control ${a:0:16}  patched ${b:0:16}"; gate $([[ $a == "$b" ]] && echo 0 || echo 1) "info.tsv digest"
  a=$(fasta_digest "$OUT/control_t32.tx.fa"); b=$(fasta_digest "$OUT/patched_t32.tx.fa")
  echo "sorted fasta  control ${a:0:16}  patched ${b:0:16}"; gate $([[ $a == "$b" ]] && echo 0 || echo 1) "fasta digest (wrap-agnostic, name-sorted)"
  a=$(gbwt_digest control_t32 "$CONTROL"); b=$(gbwt_digest patched_t32 "$PATCHED")
  echo "-b gbwt paths control ${a:0:16}  patched ${b:0:16}"; gate $([[ $a == "$b" ]] && echo 0 || echo 1) "-b GBWT digest (name, length, step count)"
  a=$(node_seq_digest control_t32 "$CONTROL"); b=$(node_seq_digest patched_t32 "$PATCHED")
  echo "node seqs     control ${a:0:16}  patched ${b:0:16}"; gate $([[ $a == "$b" ]] && echo 0 || echo 1) "node sequence multiset"
  a=$(timed control_t32.stats "$CONTROL" stats -N -E -l -z "$OUT/control_t32.pg" | tr '\n' ' ')
  b=$(timed patched_t32.stats "$PATCHED" stats -N -E -l -z "$OUT/patched_t32.pg" | tr '\n' ' ')
  echo "vg stats      control: $a"; echo "vg stats      patched: $b"; gate $([[ $a == "$b" ]] && echo 0 || echo 1) "vg stats -N -E -l -z"

  echo "# step 3: max RSS (KiB)"
  for l in $([[ $SKIP_T1 == 1 ]] || echo control_t1_a patched_t1) control_t32 patched_t32; do printf '%-13s %s KiB  wall %s\n' "$l" "$(rss_kib "$l")" "$(wall "$l")"; done
  c=$(rss_kib control_t32); p=$(rss_kib patched_t32)
  [[ -n $c && -n $p && $c -gt 0 ]] && echo "patched/control at -t 32: $(python3 -c "print(f'{$p/$c:.3f}')")  (run-to-run floor measured elsewhere: 0.03%)"

  echo "# step 4: unit tests"
  if timed unit_test "$PATCHED" test "[transcriptome]" > "$OUT/unit_test.log" 2>&1; then gate 0 "vg test [transcriptome] ($(grep -cE 'assertion|passed' "$OUT/unit_test.log") lines; see unit_test.log)"; else gate 1 "vg test [transcriptome] (see $OUT/unit_test.log)"; fi

  echo "# step 5: vg validate on the patched -t 32 graph"
  v=$(timed patched_t32.validate "$PATCHED" validate "$OUT/patched_t32.pg" 2>&1 | tail -1)
  echo "validate      $v"; gate $([[ $v == *valid* && $v != *invalid* ]] && echo 0 || echo 1) "vg validate"

  echo "# result"
  if [[ $FAIL == 0 ]]; then echo "ALL GATES PASSED"; else echo "ONE OR MORE GATES FAILED"; fi
} | tee "$OUT/REPORT.txt"
grep -q '^ALL GATES PASSED' "$OUT/REPORT.txt"
