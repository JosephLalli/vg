#!/usr/bin/env python3
"""Strict PackedGraph wire comparison for bounded RNA workspace fixtures.

Only `graph_iv` and `edge_lists_iv` may differ physically. Their PagedVector
anchors, widths, and compressed words may retain construction history, but all
decoded values and structural counts must match. Every byte before, between,
and after those two fields must match exactly. This supports the checked-out
STL PackedGraph wire format only; malformed, truncated, or unfamiliar files
are rejected rather than treated as compression-only differences.

The footer and path payload are not independently decoded here: their complete
post-edge byte range must match, and the fixture driver separately runs `vg
validate` on the candidate.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct


MAGIC = 3080648541
MAX_FILE_BYTES = 1 << 30
UINT64_MAX = (1 << 64) - 1


class Reader:
    def __init__(self, path):
        self.path, self.stream, self.size = path, path.open("rb", buffering=0), path.stat().st_size
        if self.size < 0 or self.size > MAX_FILE_BYTES:
            raise ValueError(f"unsupported file size for bounded comparer: {path}")

    def tell(self): return self.stream.tell()

    def read(self, count):
        if count < 0 or count > self.size - self.tell():
            raise ValueError(f"truncated {self.path} at {self.tell()}")
        value = self.stream.read(count)
        if len(value) != count: raise ValueError(f"truncated {self.path} at {self.tell()}")
        return value

    def u64(self): return struct.unpack("<Q", self.read(8))[0]

    def skip(self, count):
        if count < 0 or self.tell() + count > self.size:
            raise ValueError(f"truncated {self.path} at {self.tell()}")
        self.stream.seek(count, 1)

    def packed(self, name, store):
        start, filled, bits = self.tell(), self.u64(), self.u64()
        width = self.read(1)[0]
        if not 1 <= width <= 64 or bits % width or filled > bits // width:
            raise ValueError(f"invalid PackedVector {name}")
        payload_bytes = ((bits + 63) // 64) * 8
        payload = self.read(payload_bytes) if store else None
        if not store: self.skip(payload_bytes)
        return {"start": start, "end": self.tell(), "filled": filled, "bits": bits,
                "width": width, "payload": payload}

    def paged(self, name, page_size, store):
        start, filled, stored_size = self.tell(), self.u64(), self.u64()
        if stored_size != page_size: raise ValueError(f"unexpected page size in {name}")
        anchors = self.packed(name + ".anchors", store)
        pages = [self.packed(f"{name}.page[{i}]", store) for i in range(anchors["filled"])]
        if filled > len(pages) * page_size: raise ValueError(f"invalid capacity in {name}")
        return {"start": start, "end": self.tell(), "filled": filled, "page_size": page_size,
                "anchors": anchors, "pages": pages}

    def deque(self, name):
        start, begin, filled = self.tell(), self.u64(), self.u64()
        vector = self.packed(name + ".vector", False)
        if filled > vector["filled"] or (vector["filled"] and begin >= vector["filled"]):
            raise ValueError(f"invalid PackedDeque {name}")
        return {"start": start, "end": self.tell(), "begin": begin, "filled": filled}


def parse(path):
    reader = Reader(path)
    try:
        if struct.unpack(">I", reader.read(4))[0] != MAGIC: raise ValueError(f"unsupported PackedGraph magic: {path}")
        fields = {"header": {"magic": MAGIC, "max_id": reader.u64(), "min_id": reader.u64()}}
        fields["graph_iv"] = reader.paged("graph_iv", 256, True)
        fields["seq_start_iv"] = reader.paged("seq_start_iv", 256, False)
        fields["seq_length_iv"] = reader.packed("seq_length_iv", False)
        fields["edge_lists_iv"] = reader.paged("edge_lists_iv", 1024, True)
        fields["nid_to_graph_iv"] = reader.deque("nid_to_graph_iv")
        fields["seq_iv"] = reader.packed("seq_iv", False)
        if reader.tell() > reader.size:
            raise ValueError(f"parsed prefix exceeds end of file: {path}")
        return fields
    finally:
        reader.stream.close()


def packed_value(vector, index, capacity=False):
    limit = vector["bits"] // vector["width"] if capacity else vector["filled"]
    if index < 0 or index >= limit: raise ValueError("decoder coverage exceeded packed vector")
    bit, width = index * vector["width"], vector["width"]
    byte, shift = bit // 8, bit % 8
    word = int.from_bytes(vector["payload"][byte:byte + 9], "little")
    return (word >> shift) & ((1 << width) - 1)


def paged_value(vector, index, stored=False):
    if index < 0 or (not stored and index >= vector["filled"]): raise ValueError("decoder coverage exceeded paged vector")
    page, local = divmod(index, vector["page_size"])
    if page >= len(vector["pages"]) or local >= vector["pages"][page]["filled"]:
        raise ValueError("decoder coverage exceeded stored paged vector")
    anchor, diff = packed_value(vector["anchors"], page), packed_value(vector["pages"][page], local)
    if diff == 0: return 0
    if diff % 5 == 0:
        if anchor < diff // 5: raise ValueError("invalid negative PagedVector delta")
        return anchor - diff // 5
    value = anchor + diff - diff // 5 - 1
    if value > UINT64_MAX: raise ValueError("PagedVector decoded value exceeds uint64")
    return value


def packed_hygiene(vector):
    capacity = vector["bits"] // vector["width"]
    for index in range(vector["filled"], capacity):
        if packed_value(vector, index, capacity=True) != 0:
            return False
    payload_bits = len(vector["payload"]) * 8
    for bit in range(vector["bits"], payload_bits):
        if vector["payload"][bit // 8] & (1 << (bit % 8)):
            return False
    return True


def layout_equal(left, right):
    if left["filled"] != right["filled"] or left["page_size"] != right["page_size"]:
        return False
    if len(left["pages"]) != len(right["pages"]) or left["anchors"]["filled"] != len(left["pages"]):
        return False
    if right["anchors"]["filled"] != len(right["pages"]): return False
    if left["anchors"]["bits"] // left["anchors"]["width"] != right["anchors"]["bits"] // right["anchors"]["width"]:
        return False
    return all(a["filled"] == b["filled"] and a["bits"] // a["width"] == b["bits"] // b["width"]
               for a, b in zip(left["pages"], right["pages"]))


def logical_equal(left, right):
    if not layout_equal(left, right): return {"identical": False, "reason": "layout"}
    if not packed_hygiene(left["anchors"]) or not packed_hygiene(right["anchors"]):
        return {"identical": False, "reason": "nonzero unused anchor capacity or tail bits"}
    checked = 0
    for page_index, (a_page, b_page) in enumerate(zip(left["pages"], right["pages"])):
        if not packed_hygiene(a_page) or not packed_hygiene(b_page):
            return {"identical": False, "reason": "nonzero unused page capacity or tail bits", "page": page_index}
        for local in range(a_page["filled"]):
            index = page_index * left["page_size"] + local
            a, b = paged_value(left, index, stored=True), paged_value(right, index, stored=True)
            if index >= left["filled"] and (a != 0 or b != 0):
                return {"identical": False, "reason": "nonzero unused paged slot", "index": index,
                        "control": a, "candidate": b}
            if a != b: return {"identical": False, "first_difference": {"index": index, "control": a, "candidate": b}}
            checked += 1
    return {"identical": True, "values_checked": checked, "logical_filled": left["filled"]}


def same_range(left_path, left_start, left_end, right_path, right_start, right_end):
    if min(left_start, left_end, right_start, right_end) < 0 or left_start > left_end or right_start > right_end:
        return {"identical": False, "reason": "invalid range"}
    if left_end > left_path.stat().st_size or right_end > right_path.stat().st_size:
        return {"identical": False, "reason": "range past EOF"}
    if left_end - left_start != right_end - right_start:
        return {"identical": False, "reason": "length", "control_bytes": left_end-left_start, "candidate_bytes": right_end-right_start}
    first_hash, second_hash = hashlib.sha256(), hashlib.sha256()
    identical = True
    with left_path.open("rb", buffering=0) as first, right_path.open("rb", buffering=0) as second:
        first.seek(left_start); second.seek(right_start); remaining = left_end - left_start
        while remaining:
            count = min(1024 * 1024, remaining); a, b = first.read(count), second.read(count)
            if len(a) != count or len(b) != count: return {"identical": False, "reason": "short read"}
            first_hash.update(a); second_hash.update(b)
            if a != b: identical = False
            remaining -= count
    return {"identical": identical, "bytes": left_end-left_start,
            "sha256": {"control": first_hash.hexdigest(), "candidate": second_hash.hexdigest()}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("control", type=Path); parser.add_argument("candidate", type=Path); parser.add_argument("out", type=Path)
    args = parser.parse_args(); args.out.parent.mkdir(parents=True, exist_ok=True)
    if args.out.exists(): raise FileExistsError(args.out)
    report = {"control": str(args.control.resolve()), "candidate": str(args.candidate.resolve()), "passed": False}
    try:
        left, right = parse(args.control), parse(args.candidate)
        graph, edges = left["graph_iv"], left["edge_lists_iv"]
        candidate_graph, candidate_edges = right["graph_iv"], right["edge_lists_iv"]
        segments = {
            "before_graph_iv": same_range(args.control, 0, graph["start"], args.candidate, 0, candidate_graph["start"]),
            "between_graph_and_edge": same_range(args.control, graph["end"], edges["start"], args.candidate, candidate_graph["end"], candidate_edges["start"]),
            "after_edge_lists_iv": same_range(args.control, edges["end"], args.control.stat().st_size, args.candidate, candidate_edges["end"], args.candidate.stat().st_size),
        }
        scalar_headers = {"header": left["header"] == right["header"],
                          "graph_layout": layout_equal(graph, candidate_graph),
                          "edge_layout": layout_equal(edges, candidate_edges)}
        logical = {"graph_iv": logical_equal(graph, candidate_graph), "edge_lists_iv": logical_equal(edges, candidate_edges)}
        report.update({"file_bytes": {"control": args.control.stat().st_size, "candidate": args.candidate.stat().st_size},
                       "scalar_headers": scalar_headers, "exact_segments": segments, "logical": logical})
        report["passed"] = all(scalar_headers.values()) and all(item["identical"] for item in segments.values()) and all(item["identical"] for item in logical.values())
    except Exception as error:
        report["error"] = repr(error)
    args.out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"passed": report["passed"], "receipt": str(args.out)}, sort_keys=True))
    return 0 if report["passed"] else 1


if __name__ == "__main__": raise SystemExit(main())
