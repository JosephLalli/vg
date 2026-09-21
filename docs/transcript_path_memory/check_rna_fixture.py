#!/usr/bin/env python3
"""Compare an RNA candidate to a completed small, exact-recipe control."""

import argparse
import filecmp
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("control", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--workspace", action="store_true", help="enable --path-workspace")
    parser.add_argument("--reserve-gib", type=int,
                        help="reserve this many GiB in the workspace's initial sparse mapping")
    parser.add_argument("--fingerprint", type=Path, required=True)
    args = parser.parse_args()
    if args.reserve_gib is not None and not args.workspace:
        raise RuntimeError("--reserve-gib requires --workspace")
    if json.loads((args.control / "status.json").read_text())["exit_code"] != 0:
        raise RuntimeError("control is not terminal-successful")
    if (args.control / "stdout").stat().st_size > 1024**3:
        raise RuntimeError("this driver is for bounded fixtures, not full graphs")
    command = json.loads((args.control / "command.json").read_text())
    if Path(command["cwd"]).resolve() != Path.cwd():
        raise RuntimeError("run from the control's recorded working directory")
    argv = command["argv"]
    if argv[1] != "rna":
        raise RuntimeError("expected an RNA control")
    threads = int(argv[argv.index("-t") + 1])
    expected_info = Path(argv[argv.index("-i") + 1])
    args.output.mkdir(parents=True, exist_ok=False)
    info = args.output / "info.tsv"
    workspace = args.output / "workspace"
    argv[0] = str(args.binary.resolve())
    argv[argv.index("-i") + 1] = str(info)
    if args.workspace:
        argv[2:2] = ["--path-workspace", str(workspace)]
    if args.reserve_gib is not None:
        argv[2:2] = ["--path-workspace-reserve", str(args.reserve_gib)]
    measured = args.output / "measured"
    subprocess.run([sys.executable, str(Path(__file__).with_name("run_measured.py")),
                    str(measured), *argv], check=True)

    fingerprints = {}
    for label, path in [("control", args.control / "stdout"), ("candidate", measured / "stdout")]:
        receipt = args.output / (label + "-fingerprint.json")
        with receipt.open("x") as stream:
            subprocess.run([str(args.fingerprint.resolve()), str(path)], stdout=stream, check=True)
        fingerprints[label] = json.loads(receipt.read_text())
    with (args.output / "validate.stderr").open("w") as err:
        valid = subprocess.run([argv[0], "validate", str(measured / "stdout")],
                               stdout=subprocess.DEVNULL, stderr=err).returncode == 0
    graph_byte_identity = filecmp.cmp(args.control / "stdout", measured / "stdout", shallow=False)
    packed_backend_history_exception = False
    packed_wire = None
    if args.workspace:
        packed_wire_receipt = args.output / "packed-wire-comparison.json"
        packed_wire_runner = Path(__file__).with_name("compare_rna_packed_wire.py")
        packed_wire_result = subprocess.run([sys.executable, str(packed_wire_runner),
                                              str(args.control / "stdout"), str(measured / "stdout"),
                                              str(packed_wire_receipt)], check=False)
        if not packed_wire_receipt.is_file():
            raise RuntimeError("packed wire comparer did not retain a receipt")
        packed_wire = json.loads(packed_wire_receipt.read_text())
        packed_backend_history_exception = (not graph_byte_identity and packed_wire_result.returncode == 0
                                            and packed_wire.get("passed") is True)
    accepted = {
        "threads": threads,
        "workspace_enabled": args.workspace,
        "workspace_reserve_gib": args.reserve_gib,
        "graph_byte_identity": graph_byte_identity,
        "graph_fingerprint_identity": fingerprints["control"] == fingerprints["candidate"],
        "transcript_info_byte_identity": filecmp.cmp(expected_info, info, shallow=False),
        "graph_validate": valid,
        "workspace_retained": workspace.is_dir() if args.workspace else None,
        "packed_backend_history_exception": packed_backend_history_exception,
        "packed_wire_comparison": packed_wire,
    }
    (args.output / "acceptance.json").write_text(json.dumps(accepted, indent=2) + "\n")
    print(json.dumps(accepted), flush=True)
    checks = [accepted[k] for k in ("graph_fingerprint_identity", "transcript_info_byte_identity", "graph_validate")]
    if threads == 1:
        checks.append(accepted["graph_byte_identity"] or (args.workspace and accepted["packed_backend_history_exception"]))
    if args.workspace:
        checks.append(accepted["workspace_retained"])
    if not all(checks):
        raise RuntimeError("RNA fixture acceptance failed")


if __name__ == "__main__":
    main()
