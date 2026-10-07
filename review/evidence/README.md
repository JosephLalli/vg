# Recorded validation

See [performance evidence](../PERFORMANCE.md) for a readable comparison of the
recorded results, measured source revisions and limits of each measurement.

`current-vg-builds.json` records the exact source tree, dependency pins and full-build binary for each current consumer. `provenance.json` records benchmark hashes, native summaries and resource limits. Command receipts compare real independently built vg executables; GCSA receipts compare resident/external pairs, reloads, failures and scratch cleanup.

`serialization-current.json`, `distance-current.json` and `prune-current.json` contain the balanced three-cycle ABBA results. Their scope excludes setup only where explicitly stated. Representation digests are isolated probes; distance equivalence is separately established by 1,328 serialized/reloaded queries. No whole-genome timing or memory saving is inferred.

The owner-library receipts from the preceding extraction are retained as dated evidence. Their independently tested candidate commits correspond to the public adoption trees; they do not replace the newer full-vg integration receipts. GBWT baseline assertion failures are recorded separately and are not counted as passing.

The standalone output sources are based on stock vg and independent of the RNA representation series. `shared-output-integration.json` preserves separate validation of the combined storage/output implementation. All code changes need supported-platform CI and owner acceptance before upstream integration.
