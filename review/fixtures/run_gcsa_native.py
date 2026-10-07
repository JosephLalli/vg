#!/usr/bin/env python3
"""Bounded serial bytes, reload and publication-failure checks for build_gcsa."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--builder', type=Path, required=True)
parser.add_argument('--fixtures', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--threads', type=int, default=1)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
scratch = args.output / 'scratch'
scratch.mkdir(exist_ok=True)
(args.output / 'empty.gcsa2').write_text('')
rows = []

def limits():
    resource.setrlimit(resource.RLIMIT_AS, (2 * 1024**3, 2 * 1024**3))
    resource.setrlimit(resource.RLIMIT_NOFILE, (64, 64))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

def run(label, source, options, prefix, expected_success=True):
    command = [str(args.builder), '-t', '-T', str(args.threads), '-D', str(scratch), '-v', '-o', str(prefix)] + options + [str(source)]
    with (args.output / (label + '.log')).open('w') as log:
        result = subprocess.run(command, env=dict(os.environ, TMPDIR=str(scratch)),
                                stdout=log, stderr=subprocess.STDOUT, preexec_fn=limits, timeout=90)
    assert (result.returncode == 0) == expected_success, (label, result.returncode)
    assert not list(scratch.iterdir()), (label, 'scratch leak')
    row = dict(label=label, command=command, exit=result.returncode, scratch_empty=True)
    rows.append(row)
    return row

for name, source, extra in [
    ('empty', args.output / 'empty', []),
    ('zero', args.fixtures / 'cycle', ['-d', '0']),
    ('cycle', args.fixtures / 'cycle', []),
]:
    reference = None
    for route in ['resident', 'external']:
        prefix = args.output / (name + '-' + route)
        options = extra + (['--memory-limit', '1M'] if route == 'external' else [])
        row = run(name + '-' + route, source, options, prefix)
        payload = tuple(Path(str(prefix) + '.' + suffix).read_bytes() for suffix in ['gcsa', 'lcp'])
        if reference is None:
            reference = payload
        assert payload == reference, (name, route, 'paired bytes differ')
        row['sha256'] = [hashlib.sha256(data).hexdigest() for data in payload]
        row['paired_bytes_equal'] = True
        run(name + '-' + route + '-reload', source, ['-L'], prefix)

run('insufficient-memory', args.fixtures / 'cycle', ['--memory-limit', '1'],
    args.output / 'insufficient', False)

# A pre-rename failure preserves a prior GCSA destination.
prefix = args.output / 'blocked-gcsa'
Path(str(prefix) + '.gcsa').mkdir(exist_ok=True)
run('gcsa-publication-failure', args.fixtures / 'cycle', ['--memory-limit', '1M'], prefix, False)
assert Path(str(prefix) + '.gcsa').is_dir()
assert not Path(str(prefix) + '.lcp').exists()

# Paired publication is ordered: GCSA can succeed before LCP fails.
prefix = args.output / 'blocked-lcp'
Path(str(prefix) + '.lcp').mkdir(exist_ok=True)
run('lcp-publication-failure', args.fixtures / 'cycle', ['--memory-limit', '1M'], prefix, False)
assert Path(str(prefix) + '.lcp').is_dir()
assert Path(str(prefix) + '.gcsa').read_bytes() == (args.output / 'cycle-resident.gcsa').read_bytes()
rows[-1]['complete_gcsa_preserved_after_lcp_failure'] = True

(args.output / 'receipt.json').write_text(json.dumps(rows, indent=2) + '\n')
print('GCSA native: serial bytes/reload and failure cleanup passed')
