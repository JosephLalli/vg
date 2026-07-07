# Repository instructions

Agent guidance for this `vg` fork lives in **[AGENTS.md](AGENTS.md)** — read it
first. The two fork-specific essentials:

- **Build with `./build-local.sh`, never a bare `make`** (Homebrew toolchain
  drift). Details, incremental workflow, and fork patches:
  **[BUILDING-LOCAL.md](BUILDING-LOCAL.md)**.
- The **`vg mpmap --trace-splice-search`** splice-search instrumentation
  (disabled by default, changes no mapping behavior). Flags, JSONL schema, field
  reference, and what it can/cannot answer: **[MPMAP-SPLICE-TRACE.md](MPMAP-SPLICE-TRACE.md)**.

Smoke test for the instrumentation: `cd test && prove -v t/35_vg_mpmap_trace.t`.
