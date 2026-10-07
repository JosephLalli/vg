#!/usr/bin/env python3
"""Controlled bounded comparisons, with scope and output identity recorded."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import statistics
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--mode', choices=['serialization', 'distance', 'prune'], required=True)
parser.add_argument('--baseline', required=True)
parser.add_argument('--candidate', required=True)
parser.add_argument('--staging')
parser.add_argument('--fixtures', type=Path)
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
    timing = root / (label + '.time')
    with (root / (label + '.log')).open('wb') as errors:
        stream = Path(output).open('wb') if output else subprocess.PIPE
        try:
            result = subprocess.run(['/usr/bin/time', '-f', '%e %M', '-o', str(timing)]
                                    + [str(x) for x in command], env=env, stdout=stream,
                                    stderr=errors, preexec_fn=limits, timeout=180)
        finally:
            if output:
                stream.close()
    if result.returncode:
        raise RuntimeError(f'{label}: exit {result.returncode}; see {label}.log')
    elapsed, rss = timing.read_text().split()
    row = dict(label=label, command=[str(x) for x in command],
               wall_seconds=float(elapsed), peak_rss_kib=int(rss))
    rows.append(row)
    return Path(output).read_bytes() if output else result.stdout, row

def digest(path):
    checksum = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            checksum.update(block)
    return checksum.hexdigest()

summary = {}
order = ['baseline', 'candidate', 'candidate', 'baseline'] * 3
if args.mode == 'serialization':
    hashes = []
    for i, arm in enumerate(order):
        output = root / f'{i}-{arm}.pg'
        payload, row = run([getattr(args, arm), output], f'{i}-{arm}')
        row.update(arm=arm, stage_seconds=float(payload), output_bytes=output.stat().st_size,
                   output_sha256=digest(output))
        hashes.append(row['output_sha256'])
    assert len(set(hashes)) == 1, 'ordinary graph bytes differ'
    for arm in ['baseline', 'candidate']:
        arm_rows = [x for x in rows if x.get('arm') == arm]
        summary[arm] = dict(median_stage_seconds=statistics.median(x['stage_seconds'] for x in arm_rows),
                            median_peak_rss_kib=statistics.median(x['peak_rss_kib'] for x in arm_rows))
    scope = 'final graph output only; setup excluded from stage time and included in process RSS'
elif args.mode == 'distance':
    reference = None
    for arm in ['baseline', 'candidate']:
        payload, row = run([getattr(args, arm), root / (arm + '.dist')], arm + '-queries')
        payload = b''.join((root / (arm + '.dist.' + kind + '.dist.queries.tsv')).read_bytes()
                           for kind in ['ordinary', 'oversized', 'root'])
        assert len(payload.splitlines()) == 1328
        if reference is None:
            reference = payload
        assert payload == reference, 'serialized/reloaded distance queries differ'
        row.update(queries=len(payload.splitlines()), query_sha256=hashlib.sha256(payload).hexdigest())
    assert args.staging, '--staging is required for distance comparisons'
    for density in ['dense', 'sparse']:
        input_digests = []
        for i, arm in enumerate(['map', 'dense', 'dense', 'map'] * 3):
            payload, row = run([args.staging, arm, density], f'{density}-{i}-{arm}')
            values = dict(field.split('=', 1) for field in payload.decode().split())
            row.update(arm=arm, density=density, stage_seconds=float(values['seconds']),
                       records=int(values['records']), input_digest=values['input_digest'])
            input_digests.append(values['input_digest'])
        assert len(set(input_digests)) == 1
        for arm in ['map', 'dense']:
            arm_rows = [x for x in rows if x.get('density') == density and x.get('arm') == arm]
            summary[density + '-' + arm] = dict(
                median_stage_seconds=statistics.median(x['stage_seconds'] for x in arm_rows),
                median_peak_rss_kib=statistics.median(x['peak_rss_kib'] for x in arm_rows))
    scope = 'isolated staging with unchanged vg map reservation; query equivalence checked separately'
else:
    assert args.fixtures, '--fixtures is required for prune'
    graph = root / 'input.pg'
    gbwt = root / 'input.gbwt'
    run([args.baseline, 'convert', '-g', '-p', args.fixtures / 'prune.gfa'], 'prepare-graph', graph)
    run([args.baseline, 'gbwt', '-G', '--max-node', '0', '-o', gbwt,
         args.fixtures / 'prune-haplotypes.gfa'], 'prepare-gbwt')
    reference = None
    for i, arm in enumerate(order):
        binary = getattr(args, arm)
        graph_out, mapping = root / f'{i}-{arm}.pg', root / f'{i}-{arm}.map'
        _, row = run([binary, 'prune', '-t', '4', '-u', '-g', gbwt, '-m', mapping,
                      '-k', '3', '-e', '1', '-s', '0', '-M', '1', graph], f'{i}-{arm}', graph_out)
        gfa, _ = run([binary, 'convert', '-f', graph_out], f'{i}-{arm}-manifest')
        semantic = sorted(gfa.splitlines()), mapping.read_bytes()
        if reference is None:
            reference = semantic
        assert semantic == reference, 'prune graph or mapping differs'
        row.update(arm=arm, mapping_sha256=digest(mapping), graph_manifest_equal=True)
    for arm in ['baseline', 'candidate']:
        arm_rows = [x for x in rows if x.get('arm') == arm]
        summary[arm] = dict(median_wall_seconds=statistics.median(x['wall_seconds'] for x in arm_rows),
                            median_peak_rss_kib=statistics.median(x['peak_rss_kib'] for x in arm_rows))
    scope = 'whole prune command including graph load and output, excluding fixture/index preparation'
assert not list(scratch.iterdir()), 'scratch files remain'
(root / 'receipt.json').write_text(json.dumps(dict(mode=args.mode, scope=scope, order='ABBA x3',
                                                 summary=summary, rows=rows), indent=2) + '\n')
print(json.dumps(summary, indent=2))
