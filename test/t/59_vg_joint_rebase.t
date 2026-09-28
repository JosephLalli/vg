#!/usr/bin/env bash
#
# Join after prune: each chromosome is pruned in its own local node IDs and the
# products are moved into one joint ID space afterwards. The reference is the
# recorded route, where the graphs are joined with vg ids -j first and every
# prune appends to one mapping with -a. Both must give the same GCSA2.

BASH_TAP_ROOT=../deps/bash-tap
. ../deps/bash-tap/bash-tap-bootstrap

PATH=../bin:$PATH # for vg

plan tests 21

# The first 16 bytes of a node mapping are first_node and next_node.
mapping_header() {
    od -A n -t u8 -N 16 "$1" | awk '{print $1 ":" $2}'
}
mapping_entries() {
    od -A n -v -t u8 -j 16 "$1" | tr -s ' ' '\n' | grep -v '^$' | sort -n
}

# Two toy chromosomes, each in local IDs 1..69, as PackedGraph like production
for c in x y; do
    vg construct -m 32 -r small/xy.fa -v small/xy2.vcf.gz -R ${c} -C -a 2> /dev/null | vg convert -p - > jr_${c}.pg
done

# Recorded route: joint IDs first, then a guide and an appending prune per chromosome
cp jr_x.pg jr_xj.pg
cp jr_y.pg jr_yj.pg
vg ids -j -m jr_chain.mapping jr_xj.pg jr_yj.pg
for c in xj yj; do
    vg gbwt -o jr_${c}.gbwt -v small/xy2.vcf.gz -x jr_${c}.pg
    vg prune -u -e 1 -t 1 -g jr_${c}.gbwt -a -m jr_chain.mapping jr_${c}.pg > jr_${c}.pruned.pg
    vg paths -L -x jr_${c}.pg > jr_${c}.names
    vg paths -d -p jr_${c}.names -x jr_${c}.pg > jr_${c}.pathfree.pg
done
vg index -t 1 -g jr_chain.gcsa -f jr_chain.mapping jr_xj.pruned.pg jr_yj.pruned.pg

# Join after prune: x is pruned from the seed vg ids -m writes and y without one
for c in x y; do
    vg gbwt -o jr_${c}.gbwt -v small/xy2.vcf.gz -x jr_${c}.pg
done
vg ids -m jr_x.mapping jr_x.pg
vg prune -u -e 1 -t 1 -g jr_x.gbwt -a -m jr_x.mapping jr_x.pg > jr_x.pruned.pg
vg prune -u -e 1 -t 1 -g jr_y.gbwt -m jr_y.mapping jr_y.pg > jr_y.pruned.pg
vg prune -u -e 1 -t 1 -g jr_x.gbwt -m jr_x.unseeded.mapping jr_x.pg > /dev/null
is "$(mapping_header jr_y.mapping | cut -d : -f 1)" "$(( $(vg stats -r jr_y.pg | cut -f 2 | cut -d : -f 2) + 1 ))" "an unseeded local prune starts its duplicates after the local max node ID"
is "$(cmp -s jr_x.mapping jr_x.unseeded.mapping; echo $?)" 0 "a local prune seeded by vg ids -m writes the same mapping as an unseeded one"

vg ids --join-mappings jr_joint.mapping jr_x.mapping jr_y.mapping > jr_plan.tsv
is $? 0 "local node mappings can be joined"
is "$(tail -n +2 jr_plan.tsv | cut -f 2-)" "$(printf '69\t70\t29\t0\t69\n69\t70\t28\t69\t98')" "the join plan gives each part's local max, duplicates and offsets"
is "$(mapping_header jr_joint.mapping)" "$(mapping_header jr_chain.mapping)" "the joint mapping covers the same duplicate IDs as the recorded route"
is "$(diff <(mapping_entries jr_joint.mapping) <(mapping_entries jr_chain.mapping) > /dev/null; echo $?)" 0 "the joint mapping maps its duplicates to the same originals as the recorded route"

while IFS=$'\t' read -r part local_max first_dup dup_count orig_offset dup_offset; do
    c=${part%.mapping}
    vg ids --rebase-pruned ${first_dup}:${orig_offset}:${dup_offset} ${c}.pruned.pg > ${c}.rebased.pg
    vg paths -L -x ${c}.pg > ${c}.names
    vg paths -d -p ${c}.names -x ${c}.pg > ${c}.pathfree.pg
    vg ids -i ${orig_offset} ${c}.pathfree.pg > ${c}.pathfree.joint.pg
done < <(tail -n +2 jr_plan.tsv)
is "$(cmp -s jr_x.pathfree.joint.pg jr_xj.pathfree.pg && cmp -s jr_y.pathfree.joint.pg jr_yj.pathfree.pg; echo $?)" 0 "shifting the local path-free graphs by their offsets gives the joint path-free graphs"

vg index -t 1 -g jr_joint.gcsa -f jr_joint.mapping jr_x.rebased.pg jr_y.rebased.pg
is "$(cmp -s jr_joint.gcsa jr_chain.gcsa; echo $?)" 0 "the GCSA2 over the rebased pruned graphs is identical to the recorded route's"
is "$(cmp -s jr_joint.gcsa.lcp jr_chain.gcsa.lcp; echo $?)" 0 "the LCP array over the rebased pruned graphs is identical to the recorded route's"

vg index -x jr_joint.xg jr_x.pathfree.joint.pg jr_y.pathfree.joint.pg
vg ids --check-mapping jr_joint.mapping --reference jr_joint.xg jr_x.rebased.pg jr_y.rebased.pg > jr_check.tsv
is $? 0 "the rebased pruned graphs pass the mapping check"
is "$(tail -n +2 jr_check.tsv | cut -f 2-)" "$(printf '80\t29\t0\t0\n79\t28\t0\t0')" "the mapping check counts every node and duplicate"
vg ids --check-mapping jr_chain.mapping --reference jr_joint.xg jr_xj.pruned.pg jr_yj.pruned.pg > /dev/null
is $? 0 "the recorded route's pruned graphs pass the mapping check"

# One off in either offset still gives a GCSA2 that builds; only the check notices
vg ids --rebase-pruned 70:70:98 jr_y.pruned.pg > jr_y.bad_orig.pg
vg ids --rebase-pruned 70:69:97 jr_y.pruned.pg > jr_y.bad_dup.pg
vg index -t 1 -g jr_bad.gcsa -f jr_joint.mapping jr_x.rebased.pg jr_y.bad_orig.pg
is $? 0 "a GCSA2 over a graph rebased with a wrong offset builds without error"
is "$(cmp -s jr_bad.gcsa jr_chain.gcsa; echo $?)" 1 "the GCSA2 over a graph rebased with a wrong offset is wrong"
vg ids --check-mapping jr_joint.mapping --reference jr_joint.xg jr_x.rebased.pg jr_y.bad_orig.pg > /dev/null 2>&1
isnt $? 0 "the mapping check fails on a wrong original offset"
vg ids --check-mapping jr_joint.mapping --reference jr_joint.xg jr_y.bad_dup.pg > /dev/null 2>&1
isnt $? 0 "the mapping check fails on a wrong duplicate offset"

vg ids -m jr_seed.mapping jr_y.pg
vg ids --join-mappings jr_refused.mapping jr_x.mapping jr_seed.mapping > /dev/null 2>&1
isnt $? 0 "an empty seed mapping is refused as a part"
head -c 100 jr_y.mapping > jr_short.mapping
vg ids --join-mappings jr_refused.mapping jr_x.mapping jr_short.mapping > /dev/null 2>&1
isnt $? 0 "a truncated part mapping is refused"
vg ids --join-mappings jr_x.mapping jr_x.mapping jr_y.mapping > /dev/null 2>&1
isnt $? 0 "a part named as the joint mapping is refused"
is "$(cmp -s jr_x.mapping jr_x.unseeded.mapping; echo $?)" 0 "a refused join leaves its parts intact"
vg ids --rebase-pruned 70:69:68 jr_y.pruned.pg > /dev/null 2>&1
isnt $? 0 "a duplicate offset below the original offset is refused"

rm -f jr_*
