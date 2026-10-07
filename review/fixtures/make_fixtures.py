#!/usr/bin/env python3
"""Create tiny text inputs using only Python's standard library.

The default inputs fit comfortably below 1 GiB. Larger component counts are
optional timing fixtures, not representative pangenome performance estimates.
"""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('directory', type=Path)
parser.add_argument('--components', type=int, default=40)
args = parser.parse_args()
args.directory.mkdir(parents=True, exist_ok=True)
assert 0 < args.components <= 8000

nodes = ['H\tVN:Z:1.1']
walk = []
for index in range(40):
    node = index + 1
    strand = '-' if index % 3 == 1 else '+'
    nodes.append(f'S\t{node}\tACGTACGT')
    walk.append((node, strand))
for (left, ls), (right, rs) in zip(walk, walk[1:]):
    nodes.append(f'L\t{left}\t{ls}\t{right}\t{rs}\t0M')
path = ','.join(f'{node}{strand}' for node, strand in walk)
nodes.append(f'P\tchr21\t{path}\t*')
(args.directory / 'rna.gfa').write_text('\n'.join(nodes) + '\n')
# Index fragments are separate from embedded graph paths, matching the RNA
# fixture's graph/GBWT contract without installing an incomplete contig path.
haplotypes = nodes[:-1]
for sample, begin in [('sample1', 0), ('sample2', 0), ('sample3', 22)]:
    encoded = ''.join(('>' if strand == '+' else '<') + str(node)
                      for node, strand in walk[begin:])
    haplotypes.append(f'W\t{sample}\t0\tchr21\t{begin * 8}\t320\t{encoded}')
(args.directory / 'rna-haplotypes.gfa').write_text('\n'.join(haplotypes) + '\n')
for haplotype in [False, True]:
    rows = []
    for transcript in range(12):
        broken = transcript == 11
        shift = 176 if broken else 0
        contig = ('sample3#0#chr21' if broken else 'sample1#0#chr21') if haplotype else 'chr21'
        for first, last in [(2, 7), (12, 19), (26, 29)]:
            strand = '-' if transcript % 2 else '+'
            rows.append(f'{contig}\t.\texon\t{first + shift}\t{last + shift}'
                        f'\t.\t{strand}\t.\ttranscript_id "tx{transcript}";')
    name = 'rna-haplotype.gtf' if haplotype else 'rna-embedded.gtf'
    (args.directory / name).write_text('\n'.join(rows) + '\n')

graph = ['H\tVN:Z:1.1']
haplotypes = ['H\tVN:Z:1.1']
for component in range(args.components):
    a, b, h, z = (4 * component + rank for rank in range(1, 5))
    hs = '-' if component % 2 else '+'
    core = [f'S\t{node}\t{sequence}' for node, sequence in zip((a, b, h, z), 'ACGT')]
    core += [f'L\t{a}\t+\t{b}\t+\t0M', f'L\t{b}\t+\t{z}\t+\t0M',
             f'L\t{a}\t+\t{h}\t{hs}\t0M', f'L\t{h}\t{hs}\t{z}\t+\t0M']
    graph += core + [f'P\treference_{component}\t{a}+,{b}+,{z}+\t*']
    encoded = f'>{a}' + ('<' if hs == '-' else '>') + f'{h}>{z}'
    haplotypes += core + [f'W\tsample\t0\tcomponent_{component}\t0\t3\t{encoded}']
(args.directory / 'prune.gfa').write_text('\n'.join(graph) + '\n')
(args.directory / 'prune-haplotypes.gfa').write_text('\n'.join(haplotypes) + '\n')
print(args.directory)
