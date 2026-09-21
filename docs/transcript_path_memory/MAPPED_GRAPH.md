# File-backed `MappedPackedGraph` storage

This layer provides the storage primitive needed by a bounded-memory RNA path:

- `MappedPackedGraph(fd, initial_arena_bytes)` creates an empty graph whose
  allocator arena is immediately associated with a writable file. It truncates
  that file. Zero selects the existing 1 KiB initial link; a nonzero value
  reserves one sparse initial mapping and must fit `size_t` and `off_t`.
- `checkpoint_and_evict()` runs synchronous `msync` over every arena mapping,
  then `MADV_DONTNEED`. It rejects anonymous arenas. The graph remains usable
  and faults pages back from its backing file on demand.
- `serialize_packed_graph(out, interval)` emits the ordinary `PackedGraph`
  wire format and periodically checkpoints and evicts mapped input pages. It
  writes mapped packed words directly in bounded chunks; no STL graph or full
  integer vector is constructed.
- `deserialize_packed_graph(in, interval)` consumes ordinary `PackedGraph`
  wire data into an already file-associated empty graph. It validates the
  magic, exact reads, SDSL dimensions, address-size arithmetic, member lengths,
  path bounds, and deletion counters. Packed word arrays are copied directly
  into mapped storage in chunks of at most 1 MiB, with periodic checkpoint and
  eviction. A failed load retains its partial arena for diagnosis.

The standard-wire writer preserves the ordinary magic number, member order,
SDSL bit-length and width headers, 64-bit word layout, and zero tail padding.
Native `MappedPackedGraph` arena serialization is unchanged.

The inverse reader rebuilds the live path-name hash in ascending serialized
path-slot order, as ordinary `BasePackedGraph::deserialize_members()` does.
With the shared sparse-hash implementation and hash parameters, path-handle
iteration order matches ordinary deserialization. Path names are materialized
one at a time. The path-name alphabet is the only standard member stored outside
the mapped arena and is bounded to 256 bytes.

Callers must not mutate a graph concurrently with either checkpointing or
standard serialization. The interval bounds bytes deliberately faulted by the
serializer between evictions, but the kernel may perform additional readahead.
Construction stays bounded only if its caller invokes `checkpoint_and_evict()`
at suitable stage or byte boundaries.

An initial reserve uses `ftruncate()` followed by one mapping. It reserves
virtual address and logical file length without committing resident memory or
disk blocks for untouched pages. Keeping allocations in the first link avoids
mapped-pointer cross-link lookups on the hot path. It is an explicit tuning
input because checkpointing a very large mapping can add kernel page-table scan
cost. Select a full-scale value only after a matched runtime/RSS/disk-block A/B.

### Current reserve runtime evidence

On the retained RNA fixture, a 2 GiB sparse reserve stayed within one arena
link and reduced GBZ conversion from 277.104 to 209.384 seconds. The matched
ordinary graph took about 17.35 seconds, so reserve alone does not resolve the
conversion cost. Transcript construction took 16.5592 seconds with the reserve,
close to the matched ordinary time of 16.71 seconds. Region deletion fell from
476.98 to 332.978 seconds but remained far above the matched ordinary 16.83
seconds. The reserved command completed in 9:19.88 with 544,184 KiB peak RSS;
its fail-closed logical wire comparison, full graph fingerprint, transcript-info
bytes, and graph validation passed. These fixture timings do not establish a
full-scale runtime improvement. The 24-thread run completed in 8:58.29 with
545,440 KiB peak RSS; its full graph fingerprint, transcript-info bytes, and
graph validation also passed. Its raw packed layout differs from the control,
consistent with the existing thread-completion-dependent node ID and order
contract above one thread.

The remaining source-localized candidates are mapped-pointer copying and the
arena free list. Copying or moving a `yomo::Pointer` reconstructs it through a
raw-pointer assignment, which calls `Manager::get_offset_in_same_chain()` and
takes the manager lock for two address-map lookups. `allocate_from()` scans the
free list from its head, while `deallocate()` scans to find the sorted insertion
point and performs two chain-position lookups per probe. Interleaved packed
vector growth can therefore retain substantial constant or superlinear cost
even within one mapping. These are hypotheses, not measured hotspots:
`perf record` was denied by `perf_event_paranoid=4`, and a bounded GDB attach
timed out without producing a stack. The RNA process remained running with
`TracerPid: 0`. Receipts are under
`tmp/transcript_memory_20260915/mapped_input/rna-v1/workspace-t1/` in
`profile-convert-v1` and `profile-removal-gdb-v1`.

The libbdsg test fixture applies identical mutations to ordinary and mapped
graphs, including sparse IDs, oriented repeated steps, a circular path, and
edge/node/path deletions. It requires byte-identical standard serialization,
ordinary `PackedGraph` reload, continued access after eviction, and
anonymous-checkpoint rejection. It also loads ordinary bytes directly into a
file-backed graph, checks path iteration order and standard bytes before and
after matched mutations, rejects malformed/truncated and anonymous loads, and
verifies that a 16 MiB initial arena remains sparse.

The backing arena is a live-process workspace, not a portable graph format.
In particular, existing native arena serialization can contain non-relative
library objects. Persist final output with `serialize_packed_graph()` and load
that ordinary `PackedGraph` wire representation.

## RNA opt-in route

`vg rna --path-workspace DIR` selects this storage only for the exact bounded
recipe `-z -j -c no -r -d -i FILE`, with ordinary sorting enabled and transcript
annotation input. `DIR` must not exist. The command creates and retains:

- `graph.arena`: live file-backed `MappedPackedGraph` allocator storage;
- `edited.steps`: append-only `EditedMapping` records;
- `completed.steps`: append-only `handle_t` records, including old spans kept
  when sorted node IDs are rewritten.

All span offsets and counts are 64-bit. Finished edited vectors are spooled at
worker completion. Completed vectors are spooled after their splice edges are
created in path order. Boundary discovery, node retention, sorted-ID rewriting,
and embedding read bounded chunks; info output uses the cached completed-path
length. Graph copy, construction, remapping, embedding, and ordinary-wire
serialization periodically checkpoint and evict the mapped graph.

The workspace is retained after success or failure for diagnosis. It has no
resume or reuse contract, and a later invocation rejects an existing directory.
Intron input, projection, path collapsing, chopping, the `-B` body-generation
flag, FASTA,
transcript GBWT, updated haplotype GBWT, and reference filtering are rejected
before the workspace or requested outputs are created. Omitting
`--path-workspace` preserves the existing in-memory route.
Body and retention-pad features already present in the supplied annotation
stream remain part of that stream and are processed normally.
`--path-workspace-reserve N` requests a positive `N` GiB initial sparse arena,
is valid only with `--path-workspace`, and is checked before workspace or output
creation. The default retains the existing segmented allocator behavior.
