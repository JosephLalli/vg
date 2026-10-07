#!/usr/bin/env python3
"""Compare real vg binaries on the generated small text fixtures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--vg', action='append', required=True, metavar='NAME=PATH')
parser.add_argument('--mode', choices=['rna', 'prune', 'gcsa'], required=True)
parser.add_argument('--fixtures', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
arms = dict(item.split('=', 1) for item in args.vg)
assert len(arms) == len(args.vg)
args.fixtures = args.fixtures.resolve()
args.output = args.output.resolve()
args.output.mkdir(parents=True, exist_ok=True)
scratch = args.output / 'scratch'
scratch.mkdir(exist_ok=True)
environment = dict(os.environ, TMPDIR=str(scratch), OMP_NUM_THREADS='4')
rows = []

def limits():
    resource.setrlimit(resource.RLIMIT_AS, (2 * 1024**3, 2 * 1024**3))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

def run(command, label, output=None):
    log = args.output / (label + '.log')
    timing = args.output / (label + '.time')
    command = [str(x) for x in command]
    with log.open('wb') as errors:
        if output is None:
            result = subprocess.run(['/usr/bin/time', '-f', '%e %M', '-o', str(timing)] + command,
                                    env=environment, stdout=subprocess.PIPE, stderr=errors,
                                    preexec_fn=limits, timeout=120)
            payload = result.stdout
        else:
            with Path(output).open('wb') as stream:
                result = subprocess.run(['/usr/bin/time', '-f', '%e %M', '-o', str(timing)] + command,
                                        env=environment, stdout=stream, stderr=errors,
                                        preexec_fn=limits, timeout=120)
            payload = Path(output).read_bytes()
    if result.returncode:
        raise RuntimeError(f'{label}: exit {result.returncode}\n{log.read_text()[-3000:]}')
    elapsed, rss = timing.read_text().split()
    rows.append(dict(label=label, command=command, exit=result.returncode,
                     wall_seconds=float(elapsed), peak_rss_kib=int(rss)))
    return payload

def digest(payload):
    return hashlib.sha256(payload).hexdigest()

def fasta(payload):
    return sorted((part.splitlines()[0], ''.join(part.splitlines()[1:]))
                  for part in payload.decode().split('>')[1:])

def graph_manifest(vg, graph, label, anchored):
    text = run([vg, 'convert', '-f', graph], label).decode()
    nodes, edges, paths = {}, [], []
    for line in text.splitlines():
        fields = line.split('\t')
        if fields[0] == 'S':
            nodes[fields[1]] = fields[2]
        elif fields[0] == 'L':
            edges.append(fields[1:5])
        elif fields[0] == 'P':
            paths.append((fields[1], re.findall(r'([^,+-]+)([+-])', fields[2])))
        elif fields[0] == 'W':
            paths.append(('#'.join(fields[1:4]) + ':' + fields[4],
                          [(node, '+' if orientation == '>' else '-')
                           for orientation, node in re.findall(r'([><])([^><]+)', fields[6])]))
    anchors = {}
    for name, steps in sorted(paths):
        for rank, (node, orientation) in enumerate(steps):
            key = (name, rank, orientation)
            if node not in anchors or key < anchors[node]:
                anchors[node] = key
    if anchored:
        assert set(anchors) == set(nodes), 'This fixture requires every node to have a path anchor'
    def node_label(node, orientation='+'):
        if not anchored:
            return (node, orientation)
        name, rank, forward = anchors[node]
        return (name, rank, orientation != forward)
    def flip(orientation):
        return '-' if orientation == '+' else '+'
    edge_rows = []
    for left, ls, right, rs in edges:
        edge_rows.append(min((node_label(left, ls), node_label(right, rs)),
                             (node_label(right, flip(rs)), node_label(left, flip(ls)))))
    if anchored:
        complement = str.maketrans('ACGTNacgtn', 'TGCANtgcan')
        node_rows = [(node_label(node, anchors[node][2]),
                      sequence if anchors[node][2] == '+' else sequence.translate(complement)[::-1])
                     for node, sequence in nodes.items()]
    else:
        node_rows = [(node_label(node), sequence) for node, sequence in nodes.items()]
    path_rows = [(name, [node_label(node, orientation) for node, orientation in steps])
                 for name, steps in paths]
    return sorted(node_rows), sorted(edge_rows), sorted(path_rows)

first_vg = next(iter(arms.values()))
name = 'rna' if args.mode in ['rna', 'gcsa'] else 'prune'
graph = args.output / (name + '.pg')
run([first_vg, 'convert', '-g', '-p', args.fixtures / (name + '.gfa')], 'prepare-graph', graph)

if args.mode == 'rna':
    gbwt = args.output / 'input.gbwt'
    run([first_vg, 'gbwt', '-G', '--max-node', '0', '-o', gbwt, args.fixtures / 'rna-haplotypes.gfa'], 'prepare-gbwt')
    common = ['-c', 'no', '-r']
    cases = {
        'embedded': ['-n', args.fixtures / 'rna-embedded.gtf', '-e', '-a', '-k', '3', '-d'],
        'hap-reference': ['-n', args.fixtures / 'rna-haplotype.gtf', '-l', gbwt, '-j', '-o'],
        'hap-mutation': ['-n', args.fixtures / 'rna-haplotype.gtf', '-l', gbwt, '-j', '-k', '3', '-d'],
        'projection': ['-n', args.fixtures / 'rna-embedded.gtf', '-l', gbwt, '-a', '-k', '3'],
    }
    for threads in [1, 4]:
        for case, options in cases.items():
            reference = None
            for arm, vg in arms.items():
                label = f'{case}-{threads}-{arm}'
                out = args.output / label
                out.mkdir(exist_ok=True)
                command = [vg, 'rna', '-t', threads] + common + options + [
                    '-f', out / 'out.fa', '-i', out / 'out.tsv', '-b', out / 'out.gbwt', graph]
                run(command, label, out / 'out.pg')
                files = {suffix: (out / ('out.' + suffix)).read_bytes()
                         for suffix in ['pg', 'fa', 'tsv', 'gbwt']}
                semantic = (graph_manifest(vg, out / 'out.pg', label + '-graph', True),
                            fasta(files['fa']), sorted(files['tsv'].decode().splitlines()),
                            fasta(run([vg, 'paths', '-x', out / 'out.pg', '-g', out / 'out.gbwt', '-F'],
                                      label + '-gbwt-walks')))
                if reference is None:
                    reference = files, semantic
                assert semantic == reference[1], f'{label}: semantic outputs differ'
                if threads == 1:
                    assert files == reference[0], f'{label}: serial output bytes differ'
                rows.append(dict(label=label, output_sha256={k: digest(v) for k, v in files.items()},
                                 semantic_equal=True, exact_bytes_equal=files == reference[0]))

elif args.mode == 'prune':
    gbwt = args.output / 'input.gbwt'
    run([first_vg, 'gbwt', '-G', '--max-node', '0', '-o', gbwt,
         args.fixtures / 'prune-haplotypes.gfa'], 'prepare-gbwt')
    for threads in [1, 2, 4]:
        reference = None
        appended_reference = None
        for arm, vg in arms.items():
            label = f'prune-{threads}-{arm}'
            mapping = args.output / (label + '.map')
            output = args.output / (label + '.pg')
            run([vg, 'prune', '-t', threads, '-u', '-g', gbwt, '-m', mapping,
                 '-k', '3', '-e', '1', '-s', '0', '-M', '1', '-v', graph], label, output)
            result = graph_manifest(vg, output, label + '-graph', False), mapping.read_bytes()
            if reference is None:
                reference = result
            assert result == reference, f'{label}: graph/mapping outputs differ'
            # Append to that invocation's complete mapping using the same graph.
            # The existing optional verifier has a separately recorded upstream
            # appended-mapping assertion; compare actual graph/map results here.
            appended = args.output / (label + '-appended.pg')
            run([vg, 'prune', '-t', threads, '-u', '-a', '-g', gbwt, '-m', mapping,
                 '-k', '3', '-e', '1', '-s', '0', '-M', '1', graph],
                label + '-append', appended)
            result = graph_manifest(vg, appended, label + '-append-graph', False), mapping.read_bytes()
            if appended_reference is None:
                appended_reference = result
            assert result == appended_reference, f'{label}: appended graph/mapping outputs differ'

else:
    reference = None
    for arm, vg in arms.items():
        for external in [False, True]:
            label = f'gcsa-{arm}-' + ('external' if external else 'resident')
            output = args.output / (label + '.gcsa')
            options = ['--gcsa-memory', '16M'] if external else []
            run([vg, 'index', '-t', '1', '-g', output, '-k', '3', '-X', '2', '-V',
                 '-b', scratch] + options + [graph], label)
            result = tuple(Path(str(output) + suffix).read_bytes() for suffix in ['', '.lcp'])
            if reference is None:
                reference = result
            assert result == reference, f'{label}: ordinary GCSA/LCP bytes differ'
            assert not list(scratch.iterdir()), f'{label}: leaked scratch files'
            rows.append(dict(label=label, gcsa_sha256=digest(result[0]), lcp_sha256=digest(result[1]),
                             exact_bytes_equal=True, scratch_empty=True))
        prefix = args.output / ('gcsa-' + arm + '-existing.gcsa')
        Path(prefix).write_bytes(reference[0])
        Path(str(prefix) + '.lcp').write_bytes(reference[1])
        failures = {
            'invalid-memory': ['-g', prefix, '--gcsa-memory', 'invalid'],
            'zero-memory': ['-g', prefix, '--gcsa-memory', '0'],
            'too-small-memory': ['-g', args.output / (arm + '-tiny.gcsa'), '--gcsa-memory', '1'],
            'missing-gcsa-selection': ['--gcsa-memory', '16M'],
        }
        for case, options in failures.items():
            label = arm + '-' + case
            command = [str(x) for x in [vg, 'index', '-t', '1', '-b', scratch] + options + [graph]]
            with (args.output / (label + '.log')).open('wb') as errors:
                result = subprocess.run(command, env=environment, stdout=subprocess.PIPE, stderr=errors,
                                        preexec_fn=limits, timeout=120)
            assert result.returncode != 0, (label, 'expected rejection')
            assert not list(scratch.iterdir()), (label, 'scratch leak')
            assert Path(prefix).read_bytes() == reference[0]
            assert Path(str(prefix) + '.lcp').read_bytes() == reference[1]
            rows.append(dict(label=label, command=command, exit=result.returncode,
                             expected_rejection=True, existing_pair_preserved=True, scratch_empty=True))

(args.output / 'receipt.json').write_text(json.dumps(dict(mode=args.mode, arms=arms, rows=rows), indent=2) + '\n')
print(f'{args.mode}: all bounded command comparisons passed')
