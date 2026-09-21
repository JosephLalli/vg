#!/usr/bin/env python3
"""Bounded acceptance checks for path_graph_fingerprint.

The pinned GFA importer rejects empty P-lines, and does not expose a portable
GFA spelling for PackedGraph circularity. The microfixture records a requested
circular tag, but does not claim empty or circular path coverage.
"""

import json
import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "tmp/transcript_memory_20260915/fingerprint_micro"
VG = ROOT / "tmp/transcript_memory_20260915/xg_direct/vg-xg-direct"
HELPER = ROOT / "tmp/transcript_memory_20260915/path_graph_fingerprint"
RNA_CONTROL = ROOT / "tmp/transcript_memory_20260915/rna_fixture/control-t1/stdout"


def run(command, *, stdout=None, stderr=None, check=True, preexec_fn=None):
    return subprocess.run(command, check=check, stdout=stdout, stderr=stderr,
                          text=stdout is None, preexec_fn=preexec_fn)


def write_gfa(path, nodes, edges, paths):
    with path.open("w") as out:
        out.write("H\tVN:Z:1.0\n")
        for node_id, sequence in nodes:
            out.write(f"S\t{node_id}\t{sequence}\n")
        for left, right in edges:
            out.write(f"L\t{left[:-1]}\t{left[-1]}\t{right[:-1]}\t{right[-1]}\t0M\n")
        for name, walk, tags in paths:
            out.write(f"P\t{name}\t{walk}\t*{tags}\n")


def convert(stem, nodes, edges, paths):
    gfa = WORK / f"{stem}.gfa"
    packed = WORK / f"{stem}.pg"
    write_gfa(gfa, nodes, edges, paths)
    with packed.open("wb") as output:
        run([str(VG), "convert", "-g", str(gfa)], stdout=output)
    return packed


def fingerprint(path, *, expect_success=True):
    result = subprocess.run([str(HELPER), str(path)], check=False,
                            capture_output=True, text=True)
    if expect_success:
        if result.returncode:
            raise RuntimeError(f"fingerprint failed for {path}: {result.stderr}")
        return json.loads(result.stdout)
    if result.returncode == 0:
        raise RuntimeError(f"fingerprint unexpectedly accepted {path}")
    return {"returncode": result.returncode, "stderr": result.stderr.strip()}


def assert_changed(first, second, description):
    if first == second:
        raise RuntimeError(f"{description} did not change fingerprint JSON")


def memory_cap():
    import resource
    limit = 8 * 1024 * 1024 * 1024
    resource.setrlimit(resource.RLIMIT_AS, (limit, limit))


def main():
    if not VG.is_file() or not HELPER.is_file() or not RNA_CONTROL.is_file():
        raise SystemExit("required pinned converter, helper, or RNA control output is absent")
    if WORK.exists():
        raise SystemExit(f"refusing to overwrite existing receipt directory: {WORK}")
    WORK.mkdir(parents=True)

    # The sparse remap reverses nodes 1 and 3. Every corresponding edge and
    # path step is transformed, so it represents the same bidirected graph.
    base_nodes = [("1", "ACG"), ("2", "TT"), ("3", "GGA")]
    base_edges = [("1+", "2+"), ("2+", "3-")]
    base_paths = [
        ("alpha", "1+,2+,3-", ""),
        ("beta", "3+,1-", ""),
        ("circular_requested", "2+,1+", "\tTP:Z:circular"),
    ]
    remap_nodes = [("101", "CGT"), ("709", "TT"), ("1000003", "TCC")]
    remap_edges = [("101-", "709+"), ("709+", "1000003+")]
    remap_paths = [
        ("alpha", "101-,709+,1000003+", ""),
        ("beta", "1000003-,101+", ""),
        ("circular_requested", "709+,101-", "\tTP:Z:circular"),
    ]

    baseline = fingerprint(convert("baseline", base_nodes, base_edges, base_paths))
    sparse_flip = fingerprint(convert("sparse_flip", remap_nodes, remap_edges, remap_paths))
    if baseline != sparse_flip:
        raise RuntimeError("sparse ID relabel plus strand flip changed fingerprint JSON")
    if baseline["nodes"] != 3 or baseline["edges"] != 2 or baseline["paths"] != 3:
        raise RuntimeError(f"unexpected baseline counts: {baseline}")

    edge_changed = fingerprint(convert("edge_changed", base_nodes,
                                      base_edges + [("1+", "3+")], base_paths))
    assert_changed(baseline, edge_changed, "changed edge")
    walk_changed_paths = [("alpha", "1+,3-,2+", ""), *base_paths[1:]]
    walk_changed = fingerprint(convert("walk_changed", base_nodes, base_edges, walk_changed_paths))
    assert_changed(baseline, walk_changed, "changed named walk")
    uncovered = fingerprint(convert("uncovered", base_nodes + [("999", "A")],
                                    base_edges, base_paths), expect_success=False)

    real_json = WORK / "rna_control_t1.json"
    real_time = WORK / "rna_control_t1.time.txt"
    with real_json.open("w") as output:
        run(["/usr/bin/time", "-v", "-o", str(real_time), str(HELPER), str(RNA_CONTROL)],
            stdout=output, preexec_fn=memory_cap)
    json.loads(real_json.read_text())

    receipt = {
        "baseline": baseline,
        "sparse_id_relabel_and_strand_flip": sparse_flip,
        "changed_edge": edge_changed,
        "changed_named_walk": walk_changed,
        "uncovered_node": uncovered,
        "empty_path": "not tested: pinned GFA importer rejects P empty * *",
        "circular_path": "TP:Z:circular was retained in source GFA but the importer has no portable circularity assertion here",
        "real_rna_control": json.loads(real_json.read_text()),
    }
    (WORK / "acceptance.json").write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
