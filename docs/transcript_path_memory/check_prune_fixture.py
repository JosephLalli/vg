#!/usr/bin/env python3
"""Run a new candidate against a completed prune fixture control receipt."""

import argparse
import filecmp
import json
from pathlib import Path
import struct
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("control", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--workspace", action="store_true",
                        help="run the candidate with a new --path-workspace")
    parser.add_argument("--reserve-gib", type=int,
                        help="reserve this many GiB in the workspace's initial sparse mapping")
    args = parser.parse_args()
    if args.reserve_gib is not None and not args.workspace:
        raise RuntimeError("--reserve-gib requires --workspace")
    status = json.loads((args.control / "status.json").read_text())
    if status["exit_code"] != 0:
        raise RuntimeError("control is not terminal-successful")
    command = json.loads((args.control / "command.json").read_text())
    if Path(command["cwd"]).resolve() != Path.cwd():
        raise RuntimeError("run this fixture check from the recorded control cwd")
    argv = command["argv"]
    argv[0] = str(args.binary.resolve())
    mapping_index = argv.index("-m") + 1
    expected_mapping = Path(argv[mapping_index])
    candidate_mapping = Path(str(args.output) + ".mapping")
    if args.output.exists():
        raise FileExistsError(args.output)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Fixture guide node IDs are below this common, explicit append boundary.
    # The completed control mapping must never be used as a new input seed.
    with candidate_mapping.open("xb") as out:
        out.write(struct.pack("<QQ", 1_000_000_000, 1_000_000_000))
    argv[mapping_index] = str(candidate_mapping)
    workspace = args.output / "workspace"
    if args.workspace:
        argv[2:2] = ["--path-workspace", str(workspace)]
    if args.reserve_gib is not None:
        argv[2:2] = ["--path-workspace-reserve", str(args.reserve_gib)]
    runner = Path(__file__).with_name("run_measured.py")
    subprocess.run([sys.executable, str(runner), str(args.output), *argv], check=True)
    stderr = (args.output / "stderr").read_text()
    with (args.output / "validate.stderr").open("w") as err:
        valid = subprocess.run([argv[0], "validate", str(args.output / "stdout")],
                               stdout=subprocess.DEVNULL, stderr=err).returncode == 0
    acceptance = {
        "graph_byte_identity": filecmp.cmp(args.control / "stdout", args.output / "stdout", shallow=False),
        "mapping_byte_identity": filecmp.cmp(expected_mapping, candidate_mapping, shallow=False),
        "verification": "verification failed" not in stderr.lower(),
        "validate": valid,
        "workspace_retained": workspace.is_dir() if args.workspace else None,
        "workspace_reserve_gib": args.reserve_gib,
    }
    (args.output / "acceptance.json").write_text(json.dumps(acceptance, indent=2) + "\n")
    print(json.dumps(acceptance), flush=True)
    checks = [acceptance[key] for key in ("graph_byte_identity", "mapping_byte_identity", "verification", "validate")]
    if args.workspace:
        checks.append(acceptance["workspace_retained"])
    if not all(checks):
        raise RuntimeError("candidate failed fixture acceptance")


if __name__ == "__main__":
    main()
