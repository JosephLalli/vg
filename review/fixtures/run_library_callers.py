#!/usr/bin/env python3
"""Bounded complete-vg checks for the GBWT and XG dependency callers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--mode', choices=['gbwt', 'xg'], required=True)
parser.add_argument('--baseline', required=True)
parser.add_argument('--candidate', required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=True)
scratch = root / 'scratch'
scratch.mkdir(exist_ok=True)
env = dict(os.environ, TMPDIR=str(scratch), OMP_NUM_THREADS='4')
rows = []

def limits():
    resource.setrlimit(resource.RLIMIT_AS, (2 * 1024**3, 2 * 1024**3))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

def run(command, label, output=None):
    with (root / (label + '.log')).open('wb') as errors:
        stream = open(output, 'wb') if output else subprocess.PIPE
        try:
            result = subprocess.run([str(x) for x in command], env=env, stdout=stream,
                                    stderr=errors, preexec_fn=limits, timeout=180)
        finally:
            if output:
                stream.close()
    if result.returncode:
        raise RuntimeError(f'{label}: exit {result.returncode}; see {label}.log')
    rows.append(dict(label=label, command=[str(x) for x in command], exit=0))
    return Path(output).read_bytes() if output else result.stdout

gfa = root / 'paths.gfa'
with gfa.open('w') as stream:
    stream.write('H\tVN:Z:1.0\n')
    for node in range(1, 41):
        stream.write(f'S\t{node}\tACGT\nL\t{node}\t+\t{node % 40 + 1}\t+\t0M\n')
    for path in range(10000 if args.mode == 'gbwt' else 2000):
        walk = [str((rank + path) % 40 + 1) + '+' for rank in range(100)]
        if path % 2:
            walk = [step[:-1] + '-' for step in reversed(walk)]
        stream.write(f'P\tpath_{path}\t' + ','.join(walk) + '\t*\n')
graph = root / 'paths.pg'
run([args.baseline, 'convert', '-g', '-p', gfa], 'prepare-graph', graph)
reference = None
for arm, binary in [('baseline', args.baseline), ('candidate', args.candidate)]:
    for workers in ([1, 2, 4, 24] if args.mode == 'gbwt' else [1, 4]):
        label = f'{arm}-{workers}'
        index = root / (label + ('.gbwt' if args.mode == 'gbwt' else '.xg'))
        if args.mode == 'gbwt':
            payload = run([binary, 'gbwt', '-E', '--num-jobs', workers,
                           '-o', index, '-x', graph], label)
            payload = index.read_bytes()
            count = int(run([binary, 'gbwt', '-c', index], label + '-count'))
            assert count == 10000, (label, count)
            semantics = run([binary, 'paths', '-x', graph, '-g', index, '-F'], label + '-walks')
        else:
            run([binary, 'index', '-t', workers, '-x', index, graph], label)
            payload = index.read_bytes()
            semantics = run([binary, 'paths', '-x', index, '-F'], label + '-walks')
        current = payload, semantics
        if reference is None:
            reference = current
        assert current == reference, f'{label}: index bytes or reloaded walks differ'
        rows.append(dict(label=label, index_sha256=hashlib.sha256(payload).hexdigest(),
                         exact_bytes_equal=True, reloaded_walks_equal=True))
assert not list(scratch.iterdir()), 'scratch files remain'
(root / 'receipt.json').write_text(json.dumps(dict(mode=args.mode, rows=rows), indent=2) + '\n')
print(args.mode + ': complete-vg index bytes and reloaded walks match')
