#!/usr/bin/env python3
"""Read PackedGraph step counts without deserializing graph/path payloads.

Specific to the checked-out libbdsg STL PackedGraph serialization: network-order
magic, little-endian 64-bit members, PackedVector/SDSL variable-width vectors.
Only bounded headers are read; payloads are skipped with seek. This is an
inventory probe, not a full graph or checksum validator.
"""

import argparse
import json
import os
import struct


class Reader:
    def __init__(self, stream):
        self.stream = stream
        self.size = os.fstat(stream.fileno()).st_size
        self.bytes_read = 0

    def read(self, size):
        value = self.stream.read(size)
        if len(value) != size:
            raise ValueError("truncated header")
        self.bytes_read += size
        return value

    def u64(self):
        return struct.unpack("<Q", self.read(8))[0]

    def skip(self, size):
        if self.stream.tell() + size > self.size:
            raise ValueError("payload extends past EOF")
        self.stream.seek(size, os.SEEK_CUR)

    def packed(self):
        filled = self.u64()
        bits = self.u64()
        width = self.read(1)[0]
        if not 1 <= width <= 64 or bits % width or filled > bits // width:
            raise ValueError("invalid PackedVector/SDSL header")
        self.skip(((bits + 63) // 64) * 8)
        return filled

    def paged(self, expected_page_size):
        filled = self.u64()
        page_size = self.u64()
        if page_size != expected_page_size:
            raise ValueError(f"unexpected page size {page_size}")
        pages = self.packed()
        if pages > 1_000_000 or filled > pages * page_size:
            raise ValueError("page count exceeds probe bound or has invalid capacity")
        for _ in range(pages):
            self.packed()
        return filled


def inspect(path):
    # Unbuffered reads avoid read-ahead of skipped scientific payloads.
    with open(path, "rb", buffering=0) as stream:
        r = Reader(stream)
        if struct.unpack(">I", r.read(4))[0] != 3080648541:
            raise ValueError("not the supported PackedGraph format")
        max_id, min_id = r.u64(), r.u64()
        graph_entries = r.paged(256)
        r.paged(256)  # seq_start_iv
        r.packed()  # seq_length_iv
        edge_entries = r.paged(1024)
        r.u64()  # nid_to_graph_iv.begin_idx
        r.u64()  # nid_to_graph_iv.filled
        r.packed()
        sequence_entries = r.packed()
        r.paged(256)  # path_membership_node_iv
        membership_offset = stream.tell()
        membership_entries = r.u64()  # one entry per allocated step record
        if r.u64() != 1024:
            raise ValueError("unexpected membership page size")
        stream.seek(-48, os.SEEK_END)
        deleted_nodes, deleted_edges, deleted_memberships, deleted_bases, self_edges, deleted_self_edges = (
            r.u64() for _ in range(6)
        )
        if graph_entries % 2 or edge_entries % 2 or deleted_memberships > membership_entries:
            raise ValueError("inconsistent graph/footer counts")
        return {
            "file": os.path.abspath(path), "file_bytes": r.size,
            "min_id": min_id, "max_id": max_id,
            "node_count": graph_entries // 2 - deleted_nodes,
            "sequence_bases": sequence_entries - deleted_bases,
            "membership_header_offset": membership_offset,
            "allocated_step_records": membership_entries,
            "deleted_step_records": deleted_memberships,
            "live_step_records": membership_entries - deleted_memberships,
            "header_bytes_read": r.bytes_read,
        }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("graphs", nargs="+")
    args = parser.parse_args()
    for graph in args.graphs:
        print(json.dumps(inspect(graph), sort_keys=True), flush=True)
