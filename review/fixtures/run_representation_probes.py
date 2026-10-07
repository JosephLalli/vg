#!/usr/bin/env python3
"""Controlled, bounded representation comparisons; not whole-workflow timings."""
import json
import os
from pathlib import Path
import resource
import subprocess

root = Path(__file__).resolve().parent
directory = root / 'rna-probes'
rows = []
def limits():
    resource.setrlimit(resource.RLIMIT_AS, (2 * 1024**3, 2 * 1024**3))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
for comparison, executable, arms in [
    ('R03', 'r03-representation-benchmark', ('protobuf', 'edited')),
    ('R08', 'representation-benchmark', ('expanded', 'shared')),
]:
    for iteration, arm in enumerate([arms[0], arms[1], arms[1], arms[0]] * 3):
        timing = root / f'{comparison}-{iteration}.time'
        command = ['/usr/bin/time', '-f', '%e %M', '-o', str(timing),
                   str(directory / executable), arm]
        result = subprocess.run(command, text=True, capture_output=True, check=True,
                                timeout=60, env=dict(os.environ, OMP_NUM_THREADS='1'),
                                preexec_fn=limits)
        elapsed, rss = timing.read_text().split()
        rows.append(dict(comparison=comparison, arm=arm, order=iteration,
                         wall_seconds=float(elapsed), peak_rss_kib=int(rss),
                         output=result.stdout.strip()))
    outputs = [x['output'].split()[-1].removeprefix('digest=')
               for x in rows if x['comparison'] == comparison]
    assert len(set(outputs)) == 1, (comparison, outputs)
result = subprocess.run([str(directory / 'translation-cache-probe')],
                        text=True, capture_output=True, check=True, timeout=60,
                        preexec_fn=limits)
receipt = dict(passed=True, order='ABBA repeated three times',
               scope='isolated representation construction and traversal', rows=rows,
               translation_cache=result.stdout.strip())
(root / 'representation-probes-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print('Representation ABBA and translation-cache checks passed')
