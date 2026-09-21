#!/usr/bin/env python3
"""Summarize a run_measured.py receipt without inferring phase causality.

RSS values are periodic samples, not definitive process peaks. GNU time's peak
RSS is reported separately when present, but does not establish completion.
"""

import argparse
import json
from pathlib import Path
import re
import time


SAMPLE_FIELDS = ("VmRSS", "RssAnon", "RssFile", "VmHWM")


def jsonl(path):
    """Read JSONL, permitting only an incomplete final line in a live receipt."""
    if not path.exists():
        return [], False
    data = path.read_bytes()
    records, partial = [], False
    lines = data.splitlines(keepends=True)
    for index, raw in enumerate(lines):
        text = raw.decode("utf-8")
        try:
            records.append(json.loads(text))
        except json.JSONDecodeError:
            if index == len(lines) - 1 and not raw.endswith((b"\n", b"\r")):
                partial = True
                continue
            raise ValueError(f"invalid interior JSONL record in {path} at line {index + 1}")
    return records, partial


def sample_summary(samples):
    result = {"sample_count": len(samples)}
    for field in SAMPLE_FIELDS:
        values = [sample[field] for sample in samples if isinstance(sample.get(field), int)]
        if values:
            result["sampled_" + field + "_bytes_max"] = max(values)
    return result


def gnu_peak(path):
    if not path.exists() or not path.stat().st_size:
        return None
    match = re.search(r"Maximum resident set size \(kbytes\):\s*(\d+)", path.read_text(errors="replace"))
    return int(match.group(1)) if match else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("measured", type=Path, help="run_measured.py output directory")
    args = parser.parse_args()
    measured = args.measured
    command = json.loads((measured / "command.json").read_text())
    start = command.get("started_unix")
    if not isinstance(start, (int, float)):
        raise ValueError("command.json lacks numeric started_unix")
    phases, phases_partial = jsonl(measured / "phases.jsonl")
    samples, samples_partial = jsonl(measured / "rss.jsonl")
    for record in [*phases, *samples]:
        if not isinstance(record.get("unix"), (int, float)):
            raise ValueError("JSONL record lacks numeric unix timestamp")
    status_path = measured / "status.json"
    status = json.loads(status_path.read_text()) if status_path.exists() else None
    terminal = status is not None and isinstance(status.get("finished_unix"), (int, float))
    end = status["finished_unix"] if terminal else None
    observation = end if terminal else time.time()
    boundaries = [(start, "pre_first_message")]
    boundaries.extend((record["unix"], record.get("line", "")) for record in phases)
    intervals = []
    for index, (begin, label) in enumerate(boundaries):
        finish = boundaries[index + 1][0] if index + 1 < len(boundaries) else end
        selected = [sample for sample in samples if sample["unix"] >= begin and (finish is None or sample["unix"] < finish)]
        intervals.append({"label": label, "start_unix": begin, "end_unix": finish,
                          "elapsed_seconds": (finish if finish is not None else observation) - begin,
                          **sample_summary(selected)})
    report = {
        "measured": str(measured.resolve()), "command": command,
        "state": "terminal" if terminal else "nonterminal_receipt",
        "liveness": ("not applicable: terminal receipt" if terminal else
                     "unknown: this read-only summarizer does not inspect process liveness"),
        "terminal_status": status,
        "observation_unix": observation,
        "total_elapsed_seconds": observation - start,
        "phase_intervals": intervals,
        "sampled_overall": sample_summary(samples),
        "partial_final_jsonl": {"phases": phases_partial, "rss": samples_partial},
        "interpretation": "phase intervals are timestamp groupings only; sampled RSS maxima are not definitive peaks or causal phase measurements; a GNU time peak does not establish successful completion",
    }
    peak = gnu_peak(measured / "time.txt")
    if peak is not None:
        report["gnu_time_peak_rss_kib"] = peak
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
