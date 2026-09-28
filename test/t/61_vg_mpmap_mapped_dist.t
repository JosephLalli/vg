#!/usr/bin/env bash

BASH_TAP_ROOT=../deps/bash-tap
. ../deps/bash-tap/bash-tap-bootstrap

PATH=../bin:$PATH # for vg

# The mapping-permission checks read /proc, so they only run where it exists.
if [ -r /proc/self/maps ]; then
    plan tests 27
else
    plan tests 25
fi

# A writable mapping of a distance index changes its hash and mtime on the first lookup,
# and a mapping of a file shorter than the mapper's first link grows it, so every run
# below checks the index files' size, mtime and content hash.
fingerprint() {
    echo "$(stat -c '%s %y' "$1" 2>/dev/null || stat -f '%z %Fm' "$1") $({ sha256sum 2>/dev/null || shasum -a 256; } < "$1" | cut -f 1 -d\ )"
}

make_fastq() {
    local name="$1"
    local seq="$2"
    printf '@%s\n%s\n+\n%s\n' "${name}" "${seq}" "$(printf "%${#seq}s" '' | tr ' ' H)"
}

vg construct -m 32 -r small/x.fa -v small/x.vcf.gz > md.vg
vg index md.vg -x md.xg -g md.gcsa
vg index md.vg -j md.dist
cp md.dist md.copy.dist
# mpmap maps an index read-only only when its record pointer is already flagged local,
# which a fresh index is not; setting that one byte is how an index is prepared for sharing.
cp md.dist md.local.dist
printf '\001' | dd of=md.local.dist bs=1 seek=140 conv=notrunc 2> /dev/null
chmod u+w md.dist md.copy.dist md.local.dist

# The spliced read from 33_vg_mpmap.t
make_fastq read CAAATAAGGCTTGGAAATTTTCTGGAGTTCTATTATATTCCAACTCTCTGGCCATTTTAAGTTTCCTGTGGACTAAGGACAAAGGTGCGGGGAGATGA > md.fq

is "$(test -w md.dist && test -w md.copy.dist && test -w md.local.dist && echo writable)" "writable" "the distance indexes under test are writable"
is "$(cmp -l md.dist md.local.dist | awk '{print $1, $2, $3}')" "141 0 1" "the prepared index differs from the fresh one only in its local flag"

BEFORE="$(fingerprint md.dist)"
BEFORE_COPY="$(fingerprint md.copy.dist)"
BEFORE_LOCAL="$(fingerprint md.local.dist)"

vg mpmap -x md.xg -g md.gcsa -d md.dist --intron-dist md.dist -B -n rna -f md.fq -t 1 > md.same.gamp 2> md.same.err
is "$?" "0" "mpmap maps with one distance index shared by -d and --intron-dist"
is "$(fingerprint md.dist)" "${BEFORE}" "a shared distance index file is not modified"
is "$(grep -c 'Completed loading distance index: copied into memory because its record pointer is not flagged local' md.same.err)" "1" "a fresh distance index is copied into memory"

vg mpmap -x md.xg -g md.gcsa -d md.dist --intron-dist md.copy.dist -B -n rna -f md.fq -t 1 > md.separate.gamp
is "$?" "0" "mpmap maps with separate -d and --intron-dist files"
is "$(fingerprint md.dist) $(fingerprint md.copy.dist)" "${BEFORE} ${BEFORE_COPY}" "separately loaded distance index files are not modified"

# Background loading takes a different code path from the single-threaded load.
vg mpmap -x md.xg -g md.gcsa -d md.dist --intron-dist md.copy.dist -B -n rna -f md.fq -t 4 > md.background.gamp 2> md.background.err
is "$(grep -c 'Loading distance index from md.dist (in background)' md.background.err) $(fingerprint md.dist) $(fingerprint md.copy.dist)" "1 ${BEFORE} ${BEFORE_COPY}" "a distance index loaded in the background is not modified"

vg mpmap -x md.xg -g md.gcsa -d md.local.dist --intron-dist md.local.dist -B -n rna -f md.fq -t 1 > md.local.gamp 2> md.local.err
is "$(grep -c 'Completed loading distance index: mapped read-only' md.local.err)" "1" "a distance index flagged local is mapped read-only"
is "$(fingerprint md.local.dist)" "${BEFORE_LOCAL}" "a writable distance index mapped read-only is not modified"

vg mpmap -x md.xg -g md.gcsa -d md.dist --intron-dist md.dist -B -n rna -f md.fq -t 1 > md.repeat.gamp
is "$(md5sum < md.separate.gamp)" "$(md5sum < md.same.gamp)" "sharing the -d index with --intron-dist does not change the mapping"
is "$(md5sum < md.local.gamp)" "$(md5sum < md.same.gamp)" "a mapped distance index maps identically to a copied one"
is "$(md5sum < md.repeat.gamp)" "$(md5sum < md.same.gamp)" "a repeated run produces identical output"
# 33_vg_mpmap.t established this score when -d was always loaded by copying the stream.
is "$(vg view -KG md.local.gamp | vg view -aj - | jq .score)" "105" "the spliced alignment has the score it had under stream loading"

cp md.local.dist md.readonly.dist
chmod a-w md.readonly.dist
vg mpmap -x md.xg -g md.gcsa -d md.readonly.dist --intron-dist md.readonly.dist -B -n rna -f md.fq -t 1 > md.readonly.gamp
is "$(md5sum < md.readonly.gamp)" "$(md5sum < md.same.gamp)" "a read-only distance index maps identically"

# A pipe cannot be mapped, so it is copied into memory.
vg mpmap -x md.xg -g md.gcsa -d <(cat md.local.dist) -B -n rna -f md.fq -t 1 > md.pipe.gamp
is "$(md5sum < md.pipe.gamp)" "$(md5sum < md.same.gamp)" "a distance index read from a pipe maps identically"

# A distance index this small is shorter than the mapper's first link.
printf 'H\tVN:Z:1.0\nS\t1\tACGT\nS\t2\tG\nS\t3\tT\nS\t4\tAAAA\nL\t1\t+\t2\t+\t0M\nL\t1\t+\t3\t+\t0M\nL\t2\t+\t4\t+\t0M\nL\t3\t+\t4\t+\t0M\n' > md.tiny.gfa
vg convert -g md.tiny.gfa -p > md.tiny.pg
vg index md.tiny.pg -x md.tiny.xg -g md.tiny.gcsa
vg index md.tiny.pg -j md.tiny.dist
cp md.tiny.dist md.tiny.copy.dist
chmod u+w md.tiny.dist md.tiny.copy.dist
make_fastq tiny ACGTGAAAA > md.tiny.fq
TINY_BEFORE="$(fingerprint md.tiny.dist) $(fingerprint md.tiny.copy.dist)"
vg mpmap -x md.tiny.xg -g md.tiny.gcsa -d md.tiny.dist --intron-dist md.tiny.copy.dist -B -n rna -f md.tiny.fq -t 1 > md.tiny.gamp
is "$?" "0" "mpmap maps with distance indexes smaller than one mapped link"
is "$(test $(wc -c < md.tiny.dist) -lt 1024 && echo small) $(fingerprint md.tiny.dist) $(fingerprint md.tiny.copy.dist)" "small ${TINY_BEFORE}" "distance indexes smaller than one mapped link are not grown"

vg mpmap -x md.xg -g md.gcsa -d md.gcsa -B -n rna -f md.fq -t 1 > /dev/null 2> md.bad.err
is "$? $(grep -c 'cannot load distance index md.gcsa' md.bad.err)" "1 1" "a -d file that is not a distance index fails and names the file"

vg mpmap -x md.xg -g md.gcsa -d md.dist --intron-dist md.gcsa -B -n rna -f md.fq -t 1 > /dev/null 2> md.bad.err
is "$? $(grep -c 'cannot load distance index md.gcsa' md.bad.err)" "1 1" "an --intron-dist file that is not a distance index fails and names the file"

# A stream load only warns about a wrong magic number and then reads the file as records.
# That happens to throw for the GCSA2 above but crashes for these, unless the magic
# number is checked first.
vg mpmap -x md.xg -g md.gcsa -d md.xg -B -n rna -f md.fq -t 1 > /dev/null 2> md.bad.err
is "$? $(grep -c 'cannot load distance index md.xg' md.bad.err)" "1 1" "a graph given as -d fails and names the file"

vg mpmap -x md.xg -g md.gcsa -d md.dist --intron-dist md.xg -B -n rna -f md.fq -t 1 > /dev/null 2> md.bad.err
is "$? $(grep -c 'cannot load distance index md.xg' md.bad.err)" "1 1" "a graph given as --intron-dist fails and names the file"

: > md.empty.dist
vg mpmap -x md.xg -g md.gcsa -d md.empty.dist -B -n rna -f md.fq -t 1 > /dev/null 2> md.bad.err
is "$? $(grep -c 'cannot load distance index md.empty.dist' md.bad.err)" "1 1" "an empty -d file fails and names the file"

# A pipe is checked only as it is read, not beforehand like a regular file.
vg mpmap -x md.xg -g md.gcsa -d <(cat md.xg) -B -n rna -f md.fq -t 1 > /dev/null 2> md.bad.err
is "$? $(grep -c 'cannot load distance index /dev/fd/' md.bad.err)" "1 1" "a graph piped as -d fails and names the pipe"

vg mpmap -x md.xg -g md.gcsa -d md.dist --intron-dist <(cat md.xg) -B -n rna -f md.fq -t 1 > /dev/null 2> md.bad.err
is "$? $(grep -c 'cannot load distance index /dev/fd/' md.bad.err)" "1 1" "a graph piped as --intron-dist fails and names the pipe"

if [ -r /proc/self/maps ]; then
    # mpmap opens its reads only after loading every index, so while it waits on an
    # empty FIFO its index mappings are complete and can be inspected.
    rm -f md.fifo
    mkfifo md.fifo
    vg mpmap -x md.xg -g md.gcsa -d md.local.dist --intron-dist md.dist -B -n rna -f md.fifo -t 2 > md.fifo.gamp 2> md.fifo.err &
    MPMAP_PID=$!
    for i in $(seq 1 300); do
        if grep -q "Mapping reads" md.fifo.err || ! kill -0 ${MPMAP_PID} 2>/dev/null; then
            break
        fi
        sleep 0.1
    done
    DIST_MAPS="$(awk '$6 ~ /\/md\.(local\.)?dist$/' /proc/${MPMAP_PID}/maps 2>/dev/null)"
    timeout 60 sh -c 'cat md.fq > md.fifo'
    wait ${MPMAP_PID}
    is "$(echo "${DIST_MAPS}" | awk '{print $6}' | sed 's|.*/||' | sort -u | tr '\n' ' ')" "md.local.dist " "only the distance index flagged local is memory-mapped from its file"
    is "$(echo "${DIST_MAPS}" | awk '{print $2}' | sort -u | tr '\n' ' ')" "r--s " "the distance index mapping is read-only and shared"
    rm -f md.fifo md.fifo.gamp md.fifo.err
fi

rm -f md.vg md.xg md.gcsa md.gcsa.lcp md.dist md.copy.dist md.local.dist md.fq
rm -f md.same.gamp md.same.err md.separate.gamp md.repeat.gamp md.local.gamp md.local.err
rm -f md.background.gamp md.background.err md.pipe.gamp md.bad.err md.empty.dist md.readonly.dist md.readonly.gamp
rm -f md.tiny.gfa md.tiny.pg md.tiny.xg md.tiny.gcsa md.tiny.gcsa.lcp md.tiny.dist md.tiny.copy.dist md.tiny.fq md.tiny.gamp
