#!/usr/bin/env python3
"""Read-only semantic gate for a completed full chr21 RNA candidate.

This script imposes no CPU, memory, or wall-time limit. Invoke it only from an
externally resource-limited service. It never reruns the retained baseline.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import traceback


FIELDS = ("nodes", "edges", "paths", "node_sha256", "edge_sha256", "path_sha256")


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def stat(path):
    result = path.stat()
    return {"path": str(path), "bytes": result.st_size, "mtime_ns": result.st_mtime_ns}


def terminal_status(directory):
    status_path = directory / "status.json"
    status = json.loads(status_path.read_text())
    if status.get("exit_code") != 0:
        raise RuntimeError(f"non-successful measured receipt: {status_path}")
    return status


def current_input_stats(inputs):
    if not isinstance(inputs, dict):
        raise RuntimeError("inputs.json must map input paths to recorded stats")
    result = {}
    for text_path in inputs:
        path = Path(text_path)
        result[text_path] = stat(path)
    return result


def recorded_input_identity(inputs, current):
    result = {}
    for text_path, recorded in inputs.items():
        if not isinstance(recorded, dict):
            result[text_path] = False
            continue
        result[text_path] = (recorded.get("bytes") == current[text_path]["bytes"]
                             and recorded.get("mtime_ns") == current[text_path]["mtime_ns"])
    return result


def compare_and_hash(left, right):
    left_hash, right_hash = hashlib.sha256(), hashlib.sha256()
    identical = left.stat().st_size == right.stat().st_size
    with left.open("rb") as first, right.open("rb") as second:
        while True:
            a, b = first.read(1024 * 1024), second.read(1024 * 1024)
            left_hash.update(a)
            right_hash.update(b)
            if a != b:
                identical = False
            if not a and not b:
                break
    return {"identical": identical, "control": {**stat(left), "sha256": left_hash.hexdigest()},
            "candidate": {**stat(right), "sha256": right_hash.hexdigest()}}


def run_measured(runner, receipt, argv):
    command = [sys.executable, str(runner), str(receipt), *map(str, argv)]
    completed = subprocess.run(command, check=False)
    return {"runner_argv": command, "runner_exit_code": completed.returncode,
            "receipt": str(receipt), "gnu_time": str(receipt / "time.txt"),
            "status": json.loads((receipt / "status.json").read_text())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-run", type=Path, required=True,
                        help="full RNA run folder containing measured/, command-acceptance.json, inputs.json, info.tsv")
    parser.add_argument("--baseline-fingerprint", type=Path, required=True,
                        help="retained baseline fingerprint run folder (never rerun)")
    parser.add_argument("--fingerprint-binary", type=Path, required=True)
    parser.add_argument("--vg", type=Path, required=True, help="pinned vg binary used for read-only validate")
    parser.add_argument("--out", type=Path, required=True, help="new semantic-gate receipt directory")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    aggregate = {
        "resource_cap": "none; caller must run this script inside an externally resource-limited service",
        "candidate_run": str(args.candidate_run.resolve()),
        "baseline_fingerprint": str(args.baseline_fingerprint.resolve()),
        "fingerprint_binary": str(args.fingerprint_binary.resolve()),
        "vg": str(args.vg.resolve()),
        "passed": False,
    }
    try:
        candidate_measured = args.candidate_run / "measured"
        candidate_graph = candidate_measured / "stdout"
        candidate_status = terminal_status(candidate_measured)
        acceptance = json.loads((args.candidate_run / "command-acceptance.json").read_text())
        required_acceptance = {
            key: acceptance.get(key) is True for key in ("transcript_info_byte_identity", "inputs_unchanged")
        }
        if not all(required_acceptance.values()):
            raise RuntimeError(f"candidate acceptance prerequisite failed: {required_acceptance}")
        if not (args.candidate_run / "info.tsv").is_file():
            raise RuntimeError("candidate info.tsv is absent")
        inputs_path = args.candidate_run / "inputs.json"
        inputs = json.loads(inputs_path.read_text())
        input_stats_before = current_input_stats(inputs)
        recorded_input_matches_before = recorded_input_identity(inputs, input_stats_before)
        input_receipt_hash_before = sha256_file(inputs_path)
        if not all(recorded_input_matches_before.values()):
            raise RuntimeError(f"candidate inputs no longer match inputs.json: {recorded_input_matches_before}")
        raw_info_keys = [text_path for text_path in inputs if Path(text_path).name == "raw_info.tsv"]
        if len(raw_info_keys) != 1:
            raise RuntimeError("inputs.json must contain exactly one raw_info.tsv input")
        info_comparison = compare_and_hash(Path(raw_info_keys[0]), args.candidate_run / "info.tsv")
        if not info_comparison["identical"]:
            raise RuntimeError("candidate info.tsv no longer matches recorded raw_info.tsv")
        graph_before = stat(candidate_graph)
        baseline_status = terminal_status(args.baseline_fingerprint)
        baseline = json.loads((args.baseline_fingerprint / "stdout").read_text())
        if set(FIELDS) - set(baseline):
            raise RuntimeError("baseline fingerprint JSON lacks required fields")
        for executable in (args.fingerprint_binary, args.vg):
            if not executable.is_file():
                raise RuntimeError(f"executable is absent: {executable}")
        tool_hash_before = {"fingerprint_binary": sha256_file(args.fingerprint_binary),
                            "vg": sha256_file(args.vg)}
        runner = Path(__file__).with_name("run_measured.py")
        validate = run_measured(runner, args.out / "validate", [args.vg.resolve(), "validate", candidate_graph])
        fingerprint = run_measured(runner, args.out / "fingerprint",
                                   [args.fingerprint_binary.resolve(), candidate_graph])
        if fingerprint["status"].get("exit_code") != 0:
            raise RuntimeError("candidate fingerprint command failed")
        candidate = json.loads((args.out / "fingerprint" / "stdout").read_text())
        if set(FIELDS) - set(candidate):
            raise RuntimeError("candidate fingerprint JSON lacks required fields")
        comparisons = {field: candidate[field] == baseline[field] for field in FIELDS}
        input_stats_after = current_input_stats(inputs)
        recorded_input_matches_after = recorded_input_identity(inputs, input_stats_after)
        input_receipt_hash_after = sha256_file(inputs_path)
        graph_after = stat(candidate_graph)
        tool_hash_after = {"fingerprint_binary": sha256_file(args.fingerprint_binary),
                           "vg": sha256_file(args.vg)}
        aggregate.update({
            "candidate_measured_status": candidate_status,
            "baseline_status": baseline_status,
            "baseline_gnu_time": str(args.baseline_fingerprint / "time.txt"),
            "candidate_prerequisites": required_acceptance,
            "input_receipt_sha256_before": input_receipt_hash_before,
            "input_receipt_sha256_after": input_receipt_hash_after,
            "input_stats_before": input_stats_before,
            "input_stats_after": input_stats_after,
            "recorded_input_matches_before": recorded_input_matches_before,
            "recorded_input_matches_after": recorded_input_matches_after,
            "info_identity": info_comparison,
            "candidate_graph_stat_before": graph_before,
            "candidate_graph_stat_after": graph_after,
            "tool_sha256_before": tool_hash_before,
            "tool_sha256_after": tool_hash_after,
            "validate": validate,
            "fingerprint": fingerprint,
            "baseline_fingerprint": {field: baseline[field] for field in FIELDS},
            "candidate_fingerprint": {field: candidate[field] for field in FIELDS},
            "fingerprint_field_identity": comparisons,
        })
        aggregate["passed"] = (validate["status"].get("exit_code") == 0 and all(comparisons.values())
                               and input_stats_before == input_stats_after and graph_before == graph_after
                               and input_receipt_hash_before == input_receipt_hash_after
                               and all(recorded_input_matches_after.values())
                               and info_comparison["identical"] and tool_hash_before == tool_hash_after
                               and validate["runner_exit_code"] == 0 and validate["status"].get("exit_code") == 0
                               and fingerprint["runner_exit_code"] == 0 and fingerprint["status"].get("exit_code") == 0)
        if not aggregate["passed"]:
            raise RuntimeError("semantic gate failed")
    except Exception as error:
        aggregate["error"] = repr(error)
        aggregate["traceback"] = traceback.format_exc()
    (args.out / "acceptance.json").write_text(json.dumps(aggregate, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"passed": aggregate["passed"], "receipt": str(args.out / "acceptance.json")}, sort_keys=True))
    return 0 if aggregate["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
