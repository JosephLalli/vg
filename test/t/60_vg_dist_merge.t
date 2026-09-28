#!/usr/bin/env bash
#
# Distance indexes built per chromosome in local node IDs, merged into one index for the
# joint ID space with vg index --merge-dist. Chromosomes are disconnected, so the merged
# index must describe the same snarls and give mpmap the same answers as an index built
# on the joined graph.

BASH_TAP_ROOT=../deps/bash-tap
. ../deps/bash-tap/bash-tap-bootstrap

PATH=../bin:$PATH # for vg

plan tests 9

# Two toy chromosomes, each in local IDs starting at 1
for c in x y; do
    vg construct -m 32 -r small/xy.fa -v small/xy2.vcf.gz -R ${c} -C -a 2> /dev/null > dm_${c}.vg
    vg index -j dm_${c}.dist dm_${c}.vg
done
max_x=$(vg stats -r dm_x.vg | cut -f 2 | cut -d ':' -f 2)

# The joined graph and an index built on it
cp dm_x.vg dm_xj.vg
cp dm_y.vg dm_yj.vg
vg ids -j dm_xj.vg dm_yj.vg
vg combine dm_xj.vg dm_yj.vg > dm_union.vg
vg index -j dm_mono.dist dm_union.vg

vg index --merge-dist dm_merged.dist dm_x.dist:0 dm_y.dist:${max_x}
is $? 0 "per-chromosome distance indexes can be merged with node ID offsets"

# The merged snarls are the parts' snarls with the second part's node IDs shifted
shifted_snarls() {
    vg stats -b "$1" | grep -v '^#' | awk -v o="$2" 'BEGIN {OFS = "\t"} {$1 += o; $2 += o; print}'
}
cp dm_x.dist dm_x.copy.dist
cp dm_y.dist dm_y.copy.dist
cp dm_merged.dist dm_merged.copy.dist
cat <(shifted_snarls dm_x.copy.dist 0) <(shifted_snarls dm_y.copy.dist ${max_x}) | sort > dm_parts.snarls
vg stats -b dm_merged.copy.dist | grep -v '^#' | sort > dm_merged.snarls
is "$(wc -l < dm_merged.snarls)" "$(wc -l < dm_parts.snarls)" "the merged index has as many snarls as its parts together"
diff dm_merged.snarls dm_parts.snarls > /dev/null
is $? 0 "the merged index has exactly the parts' snarls, with IDs shifted"

# mpmap on the joined graph answers the same with the merged index as with the joined-graph index
vg index -x dm_union.xg dm_union.vg
vg prune -k 24 -e 2 dm_union.vg > dm_union.pruned.vg
vg index -g dm_union.gcsa -k 16 -t 1 dm_union.pruned.vg
vg sim -x dm_union.xg -n 300 -l 50 -e 0.01 -i 0.002 -s 11 -a > dm_reads.gam
vg mpmap -x dm_union.xg -g dm_union.gcsa -d dm_mono.dist -n rna -G dm_reads.gam -t 1 2> /dev/null | vg view -Kj - | sort > dm_mono.json
vg mpmap -x dm_union.xg -g dm_union.gcsa -d dm_merged.dist -n rna -G dm_reads.gam -t 1 2> /dev/null | vg view -Kj - | sort > dm_merged.json
is "$(wc -l < dm_merged.json)" "$(wc -l < dm_mono.json)" "mpmap emits as many records with the merged index"
diff dm_merged.json dm_mono.json > /dev/null
is $? 0 "mpmap alignments are identical with the merged and the joined-graph index"

# Bad requests are refused
vg index --merge-dist dm_bad.dist dm_x.dist:0 dm_y.dist:0 2> /dev/null
isnt $? 0 "parts whose shifted node IDs overlap are refused"
vg index --merge-dist dm_bad.dist dm_x.dist:0 dm_y.dist 2> /dev/null
isnt $? 0 "a part without an ID offset is refused"
vg index --merge-dist dm_bad.dist -j dm_other.dist dm_x.dist:0 2> /dev/null
isnt $? 0 "--merge-dist cannot be combined with building another index"
is "$(ls dm_bad.dist 2> /dev/null | wc -l)" "0" "a refused merge leaves no output"

rm -f dm_*
