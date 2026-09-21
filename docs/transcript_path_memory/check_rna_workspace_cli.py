#!/usr/bin/env python3
"""Assert that invalid RNA path-workspace requests fail before graph loading."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


SENTINEL = b"workspace CLI sentinel: must remain unchanged\n"


def replace_option(argv, option, value):
    try:
        argv[argv.index(option) + 1] = str(value)
    except ValueError as error:
        raise RuntimeError(f"control recipe lacks {option}") from error


def insert_workspace(argv, value):
    argv[2:2] = ["--path-workspace", str(value)]


def run_case(name, base_argv, output, mutate, diagnostic, *, fasta=False, existing=False):
    case = output / name
    case.mkdir()
    info = case / "info.tsv"
    info.write_bytes(SENTINEL)
    argv = list(base_argv)
    replace_option(argv, "-i", info)
    workspace = case / "workspace"
    marker = None
    if existing:
        workspace.mkdir()
        marker = workspace / "marker"
        marker.write_bytes(SENTINEL)
    fasta_path = case / "output.fa" if fasta else None
    mutate(argv, workspace, fasta_path)
    try:
        completed = subprocess.run(argv, capture_output=True, timeout=120)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"{name} exceeded 120 seconds; graph work may have started") from error
    (case / "stdout").write_bytes(completed.stdout)
    (case / "stderr").write_bytes(completed.stderr)
    stderr = completed.stderr.decode("utf-8", errors="replace")
    checks = {
        "nonzero_exit": completed.returncode != 0,
        "explicit_diagnostic": diagnostic in stderr,
        "stdout_empty": completed.stdout == b"",
        "info_sentinel_unchanged": info.read_bytes() == SENTINEL,
        "fasta_absent": fasta_path is None or not fasta_path.exists(),
        "new_workspace_absent": existing or not workspace.exists(),
        "existing_marker_unchanged": marker is None or marker.read_bytes() == SENTINEL,
    }
    passed = all(checks.values())
    receipt = {"argv": argv, "returncode": completed.returncode, "checks": checks,
               "stderr_sha256": hashlib.sha256(completed.stderr).hexdigest(), "passed": passed}
    (case / "receipt.json").write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    if not passed:
        raise RuntimeError(f"workspace CLI rejection failed for {name}: {checks}")
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("control", type=Path,
                        help="terminal-successful small RNA control directory")
    parser.add_argument("output", type=Path, help="new receipt directory")
    args = parser.parse_args()
    if not args.binary.is_file():
        raise RuntimeError("candidate binary does not exist")
    status = json.loads((args.control / "status.json").read_text())
    if status.get("exit_code") != 0:
        raise RuntimeError("control is not terminal-successful")
    command = json.loads((args.control / "command.json").read_text())
    argv = list(command["argv"])
    if len(argv) < 2 or argv[1] != "rna":
        raise RuntimeError("control recipe is not vg rna")
    if "--path-workspace" in argv:
        raise RuntimeError("control recipe already requests a workspace")
    argv[0] = str(args.binary.resolve())
    args.output.mkdir(parents=True, exist_ok=False)

    def collapse_all(command, workspace, fasta):
        insert_workspace(command, workspace)
        replace_option(command, "-c", "all")

    def fasta_output(command, workspace, fasta):
        insert_workspace(command, workspace)
        command[2:2] = ["-f", str(fasta)]

    def preexisting(command, workspace, fasta):
        insert_workspace(command, workspace)

    def empty_workspace(command, workspace, fasta):
        insert_workspace(command, "")

    def reserve_without_workspace(command, workspace, fasta):
        command[2:2] = ["--path-workspace-reserve", "2"]

    def reserve_zero(command, workspace, fasta):
        insert_workspace(command, workspace)
        command[2:2] = ["--path-workspace-reserve", "0"]

    def reserve_overflow(command, workspace, fasta):
        insert_workspace(command, workspace)
        command[2:2] = ["--path-workspace-reserve", "18446744073709551615"]

    results = {
        "collapse_all": run_case("collapse_all", argv, args.output, collapse_all,
                                 "--path-workspace currently requires the exact bounded route"),
        "fasta_output": run_case("fasta_output", argv, args.output, fasta_output,
                                 "--path-workspace currently requires the exact bounded route", fasta=True),
        "preexisting_workspace": run_case("preexisting_workspace", argv, args.output, preexisting,
                                           "Cannot create new path workspace", existing=True),
        "empty_workspace": run_case("empty_workspace", argv, args.output, empty_workspace,
                                     "--path-workspace requires a nonempty directory name"),
        "reserve_without_workspace": run_case("reserve_without_workspace", argv, args.output,
                                                reserve_without_workspace, "--path-workspace-reserve"),
        "reserve_zero": run_case("reserve_zero", argv, args.output, reserve_zero,
                                  "--path-workspace-reserve"),
        "reserve_overflow": run_case("reserve_overflow", argv, args.output, reserve_overflow,
                                      "--path-workspace-reserve"),
    }
    (args.output / "acceptance.json").write_text(json.dumps(results, indent=2, sort_keys=True) + "\n")
    print(json.dumps({name: result["passed"] for name, result in results.items()}, sort_keys=True))


if __name__ == "__main__":
    main()
