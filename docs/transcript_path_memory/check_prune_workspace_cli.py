#!/usr/bin/env python3
"""Assert that invalid prune path-workspace requests fail before mapping output."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


SENTINEL = b"prune workspace CLI sentinel: must remain unchanged\n"


def replace_option(argv, option, value):
    try:
        argv[argv.index(option) + 1] = str(value)
    except ValueError as error:
        raise RuntimeError(f"control recipe lacks {option}") from error


def insert_workspace(argv, value):
    argv[2:2] = ["--path-workspace", str(value)]


def run_case(name, base_argv, output, mutate, *, expected_error=None, existing_workspace=False,
             expect_success=False):
    case = output / name
    case.mkdir()
    argv = list(base_argv)
    mapping = case / "mapping.sentinel"
    mapping.write_bytes(SENTINEL)
    replace_option(argv, "-m", mapping)
    workspace = case / "workspace"
    marker = None
    if existing_workspace:
        workspace.mkdir()
        marker = workspace / "marker"
        marker.write_bytes(SENTINEL)
    mutate(argv, workspace, case)
    try:
        completed = subprocess.run(argv, capture_output=True, timeout=120)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"{name} exceeded 120 seconds; graph work may have started") from error
    (case / "stdout").write_bytes(completed.stdout)
    (case / "stderr").write_bytes(completed.stderr)
    stderr = completed.stderr.decode("utf-8", errors="replace")
    checks = {
        "expected_exit": completed.returncode == 0 if expect_success else completed.returncode != 0,
        "explicit_diagnostic": True if expected_error is None else expected_error in stderr,
        "mapping_sentinel_unchanged": mapping.read_bytes() == SENTINEL,
        "workspace_absent": existing_workspace or not workspace.exists(),
        "existing_marker_unchanged": marker is None or marker.read_bytes() == SENTINEL,
    }
    if not expect_success:
        checks["stdout_empty"] = completed.stdout == b""
    if not all(checks.values()):
        raise RuntimeError(f"workspace CLI check failed for {name}: {checks}")
    receipt = {"argv": argv, "returncode": completed.returncode, "checks": checks,
               "stderr_sha256": hashlib.sha256(completed.stderr).hexdigest()}
    (case / "receipt.json").write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("control", type=Path, help="terminal-successful small prune control directory")
    parser.add_argument("output", type=Path, help="new receipt directory")
    args = parser.parse_args()
    if not args.binary.is_file():
        raise RuntimeError("candidate binary does not exist")
    if json.loads((args.control / "status.json").read_text()).get("exit_code") != 0:
        raise RuntimeError("control is not terminal-successful")
    command = json.loads((args.control / "command.json").read_text())
    if Path(command["cwd"]).resolve() != Path.cwd():
        raise RuntimeError("run from the control's recorded working directory")
    argv = list(command["argv"])
    if len(argv) < 2 or argv[1] != "prune" or "--path-workspace" in argv:
        raise RuntimeError("expected a workspace-free prune control")
    argv[0] = str(args.binary.resolve())
    args.output.mkdir(parents=True, exist_ok=False)

    def existing(command, workspace, case):
        insert_workspace(command, workspace)

    def empty(command, workspace, case):
        insert_workspace(command, "")

    def stdin(command, workspace, case):
        insert_workspace(command, workspace)
        command[-1] = "-"

    def bad_magic(command, workspace, case):
        insert_workspace(command, workspace)
        bad = case / "not-packed.pg"
        bad.write_bytes(b"not a PackedGraph\n")
        command[-1] = str(bad)

    def dry_run(command, workspace, case):
        insert_workspace(command, workspace)
        command[2:2] = ["-d", "--path-workspace-reserve", "2"]

    def reserve_without_workspace(command, workspace, case):
        command[2:2] = ["--path-workspace-reserve", "2"]

    def reserve_zero(command, workspace, case):
        insert_workspace(command, workspace)
        command[2:2] = ["--path-workspace-reserve", "0"]

    def reserve_overflow(command, workspace, case):
        insert_workspace(command, workspace)
        command[2:2] = ["--path-workspace-reserve", "18446744073709551615"]

    results = {
        "existing_workspace": run_case("existing_workspace", argv, args.output, existing,
                                        expected_error="--path-workspace requires a new directory", existing_workspace=True),
        "empty_workspace": run_case("empty_workspace", argv, args.output, empty,
                                     expected_error="--path-workspace requires a nonempty new directory"),
        "stdin_unsupported": run_case("stdin_unsupported", argv, args.output, stdin,
                                       expected_error="--path-workspace requires a nonempty new directory"),
        "nonstandard_input_magic": run_case("nonstandard_input_magic", argv, args.output, bad_magic,
                                             expected_error="--path-workspace requires standard PackedGraph input"),
        "dry_run_valid": run_case("dry_run_valid", argv, args.output, dry_run, expect_success=True),
        "reserve_without_workspace": run_case("reserve_without_workspace", argv, args.output,
                                                reserve_without_workspace,
                                                expected_error="--path-workspace-reserve requires --path-workspace"),
        "reserve_zero": run_case("reserve_zero", argv, args.output, reserve_zero,
                                  expected_error="--path-workspace-reserve requires a positive GiB size"),
        "reserve_overflow": run_case("reserve_overflow", argv, args.output, reserve_overflow,
                                      expected_error="--path-workspace-reserve requires a positive GiB size"),
    }
    (args.output / "acceptance.json").write_text(json.dumps(results, indent=2, sort_keys=True) + "\n")
    print(json.dumps({name: all(result["checks"].values()) for name, result in results.items()}, sort_keys=True))


if __name__ == "__main__":
    main()
