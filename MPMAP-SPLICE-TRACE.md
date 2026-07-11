# `vg mpmap` splice-search instrumentation (`--trace-splice-search`)

This fork adds an **observational trace layer** to `vg mpmap` that measures, per
read (or per mate of a pair), the search space that mpmap's current MEM-based
seeding / clustering / soft-clip-gated splice rescue actually creates. It exists
to answer one question **empirically, not by assumption**: for short-read
paired-end RNA-seq with novel splice-junction discovery, is keeping graph MEMs
sufficient, or is a STAR-like sequential seed / MMP front end warranted?

It is **disabled by default**, changes **no mapping behavior**, and implements
**no new aligner**. When disabled, the only cost is a single boolean branch at
each collection site.

## Flags

| Flag | Meaning |
|---|---|
| `--trace-splice-search FILE` | Enable tracing; write newline-delimited JSON (JSONL) to `FILE`. |
| `--trace-truth-junctions FILE` | Optional. Load a truth-junction table (`chrom donor acceptor strand junction_id`, whitespace-separated, `#` comments allowed). Its only current effect is to switch on **per-MEM detail** emission (`mem_details`) so truth overlap can be joined externally. Internal graph→reference overlap is intentionally out of scope. |

Both are advanced/observational options — they do not appear in the normal help
output and are not part of any preset.

## Example

```bash
vg mpmap -x graph.xg -d graph.dist -g graph.gcsa -n rna -f reads.fq \
         --trace-splice-search trace.jsonl \
         [--trace-truth-junctions truth.tsv] \
         -t 1 > out.gamp
```

Use `-t 1` for **deterministic line order**. Under multiple threads the records
are correct but interleaved; sort by `(read_name, mate)` when aggregating.

## Output format

One JSONL file. The **first line** is a self-describing header record:

```json
{"record_type":"header","schema":"mpmap_splice_search_trace","schema_version":1,"note":"...","truth_junctions_loaded":0}
```

Every subsequent line is one `"record_type":"read"` record — **one per read, or
one per mate for a pair** (mate 1 and mate 2 both emitted; the pair-level splice
fields are duplicated across the two mate records because the splice search is
joint). Aggregate with any JSONL reader (`jq`, `pandas.read_json(lines=True)`,
`readr`/`jsonlite`). Read sequences are never dumped, so the file stays small and
join-friendly; join to external truth/simulation on `read_name` (+ `mate`) and
the emitted graph positions.

### Read-record fields (59; +`mem_details` when a truth file is loaded)

**Read metadata:** `read_name`, `read_length`, `is_paired`, `mate`,
`is_rna_mode`, `do_spliced_alignment` (splice search actually attempted),
`mapping_succeeded`, `best_score`, `second_best_score` (JSON `null` if none),
`mapq`.

**Seed / MEM visibility** (answers "is the true flank visible in raw MEMs, and is
it filtered / capped?"): `n_mems_total`, `n_mem_hits_total`,
`n_mems_after_filtering` (long enough to seed clustering), `n_mem_hits_after_hit_max`,
`n_mem_true_occurrences` (sum of `match_count`, pre-cap), `max_hits_per_mem`,
`sum_hits_for_short_mems`, `n_mems_hit_capped` (`match_count > nodes.size()`),
`n_high_fanout_mems`, `n_mems_by_length_bin` / `n_hits_by_length_bin` (6-element
arrays, bins `<8, 8-11, 12-15, 16-20, 21-30, >30`).

**Clusters / local alignment:** `n_clusters`, `n_cluster_graphs`,
`cluster_graph_nodes_total`, `cluster_graph_edges_total`,
`largest_cluster_graph_nodes`, `largest_cluster_graph_edges`,
`best_cluster_read_coverage`, `n_local_multipath_alignments`,
`n_alt_mappings_before_cap`, `n_alt_mappings_after_cap`.

**Splice-search aggregates:** `n_splice_anchors_considered`,
`n_anchors_gate_opened`, `n_anchors_gate_skipped` (soft-clip gate did not fire),
`n_adapter_rejected`, `splice_improved_best_alignment`, `splice_changed_primary`.

**Soft-clip / splice gate configuration** (echoed per record because it is *not*
the struct default — e.g. under `-n rna -B` it resolves to ~`6 / 1 / 8`, not
`25 / 20 / 8`, so every row must document the thresholds it was measured against):
`min_softclipped_score_for_splice`, `min_softclip_length_for_splice`,
`max_softclip_overlap`.

**Proactive bounded-MEM-window proxy** (see below): `n_proactive_intervals`,
`n_proactive_intervals_with_raw_mem_evidence`, `n_proactive_raw_mem_candidates`,
`n_proactive_candidate_windows_proxy`,
`n_proactive_candidates_that_current_gate_would_skip`,
`n_proactive_candidates_that_current_rescue_would_not_see`,
`proactive_extra_candidate_ratio`.

**Phase timers (microseconds):** `time_find_mems_usec`, `time_cluster_usec`,
`time_query_cluster_graphs_usec`, `time_align_cluster_graphs_usec`,
`time_splice_rescue_usec`, `time_proactive_proxy_usec`, `time_total_usec`,
`elapsed_usec_total`.

**Nested arrays:** `final_positions` (primary alignment graph placement),
`anchors`, `proactive_intervals`, and `mem_details` (only with a truth file).

### `anchors[]` — one entry per soft-clip gate evaluation (23 fields)

`anchor_index`, `aligned_interval_begin`, `aligned_interval_end`,
`left_tail_len`, `right_tail_len`, `left_tail_max_score`, `right_tail_max_score`,
`search_left`, `search_right` (gate opened on that side), `adapter_rejected`,
`n_aligned_splice_candidates`, `n_unaligned_cluster_candidates`,
`n_unclustered_raw_mem_hit_candidates`, `n_total_splice_candidates`,
`n_candidates_aligned`, `n_candidates_rejected_before_alignment` (failed to
align), `n_candidates_rejected_after_alignment` (aligned but not chosen),
`n_motif_pairs_examined`, `motif_pairs_capped`, `n_splice_edges_considered`,
`n_splice_alignments_produced`, `best_splice_score`, `did_splice`.

An anchor may appear more than once (an anchor re-visited after a successful
splice); group by `anchor_index` if needed.

### `proactive_intervals[]` (10 fields)

`interval_begin`, `interval_end`, `side` (-1 left / +1 right soft clip),
`anchor_id`, `tail_max_score`, `candidate_mem_count`, `candidate_hit_count`,
`best_candidate_mem_length`, `max_hit_count`, `current_gate_would_skip`.

### `mem_details[]` (only with `--trace-truth-junctions`; 7 fields)

`read_begin`, `read_end`, `length`, `match_count`, `reported_hits`, `primary`
(SMEM vs. sub-MEM), `hit_positions` (up to 16 `{node_id, offset, is_reverse}`).

## The proactive proxy (what it is, and is not)

For each soft-clipped tail of the **final** primary alignment, the proxy counts
how many raw MEM hits a proactive bounded-MEM-window front end *would* surface —
**without** applying the current soft-clip score gate. It performs only
read-coordinate arithmetic over the existing MEM pool (no graph or distance-index
queries) and never mutates mapping output. The intron-distance / co-linearity
constraint is deliberately **omitted**, so the counts are a conservative **upper
bound** on what a distance-constrained proactive stage would generate. Its whole
purpose is to answer whether proactive candidate generation would *explode* or
stay *small*. Note it runs on the post-rescue alignment, so a read whose splice
already succeeded (no residual soft clip) yields zero proactive intervals — the
interesting reads are those still soft-clipped after current rescue.

## Where the metrics are collected

New files: `src/mpmap_trace.hpp` / `src/mpmap_trace.cpp` — the trace sink (a set
of free functions plus a `thread_local` "current record" pointer), the JSONL
serializer, truth loading, RAII `TraceGuard`/`ScopedTimer`, and the proxy.
`multipath_mapper.hpp` is deliberately **not** touched (so it does not force a
full recompile).

Hooks in `src/multipath_mapper.cpp`:
- `multipath_map` / `multipath_map_paired` — read metadata, MEM histograms
  (`trace_fill_mem_metrics`), cluster-graph structure (`trace_fill_cluster_metrics`),
  final placement, phase timers, and the proxy.
- `find_spliced_alignments` (single + paired) — per-anchor gate state
  (`trace_push_anchor`), candidate pools (`trace_add_candidates`,
  `trace_add_aligned_candidates`), adapter rejections.
- `test_splice_candidates` — motif-pair counts, splice edges, produced splices —
  reached via `mpmap_trace::current()` with no signature changes.

Option parsing / sink lifecycle: `src/subcommand/mpmap_main.cpp`.

## Build and test

Build with the fork's wrapper (**never a bare `make`** — see `BUILDING-LOCAL.md`):

```bash
./build-local.sh obj/mpmap_trace.o obj/multipath_mapper.o obj/subcommand/mpmap_main.o
./build-local.sh                    # relink bin/vg
cd test && prove -v t/35_vg_mpmap_trace.t   # 31 assertions
```

`test/t/35_vg_mpmap_trace.t` is the smoke test: it confirms no file is written
when the flag is absent, the header/schema, and that read records carry the MEM /
cluster / gate / candidate / proactive / timing / anchor fields — on both a
single-end spliced read and a paired pair (where the phase timers and pair splice
outcome are populated).

## What this can and cannot answer

**Can** (maps to the MEM-vs-front-end decision):
- *Keep MEMs + proactive window vs. redesign seeding* — `mem_details` (with a
  truth file) shows whether true flanks are in the raw MEM pool;
  `n_anchors_gate_skipped` plus the real gate thresholds show whether rescue
  missed them because the gate did not fire vs. because the MEM was absent.
- *Would proactive windows explode?* — `n_proactive_raw_mem_candidates` and
  `proactive_extra_candidate_ratio` quantify the extra search space (a
  conservative upper bound).
- *Is runtime seeding-bound or motif-pair/MAPQ-bound?* — the seven `time_*_usec`
  timers (now populated for paired reads too) vs. `n_motif_pairs_examined` /
  `motif_pairs_capped` and `second_best_score` / `mapq` ambiguity.

**Cannot (yet):**
- STAR-like MMP visibility — the proxy reuses the *existing* MEM pool, so it
  cannot reveal junctions only a sequential seed search would find; that needs a
  separate MMP experiment.
- Internal graph→reference truth overlap — the trace emits MEM/final positions
  for an *external* join instead.
- `n_cluster_graphs_aligned` and per-cluster covered intervals are not emitted
  (use `n_local_multipath_alignments` and `best_cluster_read_coverage`).
