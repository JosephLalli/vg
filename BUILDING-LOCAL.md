# Building this vg fork on this machine

**Always build with `./build-local.sh` — never a bare `make`.** The system
Homebrew has drifted (protobuf 33.4 + a C++17-only, *shared-only* abseil, plus a
Homebrew `sdsl`/`divsufsort` that shadow vg's vendored copies), so a plain `make`
fails. `build-local.sh` bakes in the environment that makes vg build here; it just
calls `make` with the right settings, so all normal `make` behaviour (incremental
compilation, single targets, `make clean`) works through it.

## TL;DR

```bash
./build-toolchain.sh      # ONCE: builds the local static protobuf+abseil toolchain
./build-local.sh          # build bin/vg (incremental: only recompiles what changed)
```

## First-time setup

1. `./build-toolchain.sh`
   Builds a work-directory-specific **static** protobuf (v29.3) + abseil into
   `/mnt/ssd/lalli/vg-latest-toolchain` (override with `VG_TOOLCHAIN=/path`). This
   is what replaces the unusable Homebrew protobuf/abseil. You only ever run this
   once (or after `rm -rf` of the toolchain). ~2 min.

2. `./build-local.sh`
   First run compiles all dependencies and all of vg (~20–40 min at `-j64`).
   Subsequent runs are incremental (see below).

## Incremental builds — when you change only part of the code

`build-local.sh` runs `make`, which recompiles **only the objects whose sources
(or headers they depend on) changed**, then relinks `bin/vg`. So the normal loop
is just:

```bash
# edit src/multipath_mapper.cpp ...
./build-local.sh                       # recompiles multipath_mapper.o + relinks bin/vg
```

Only the file(s) you touched are recompiled. Editing a `.cpp` recompiles one
object (seconds to ~2 min for a big file like `multipath_mapper.cpp`) plus the
`bin/vg` relink (~1–2 min). Editing a **header** recompiles every object that
includes it (make tracks this via the `.d` files in `obj/`). Adding a brand-new
`src/*.cpp` is picked up automatically (the Makefile globs `src/*.cpp`).

### Faster: build one object without relinking

To check that a single file compiles, without paying the `bin/vg` relink, pass the
object as a target:

```bash
./build-local.sh obj/multipath_mapper.o          # ~compile only, no link
./build-local.sh obj/subcommand/mpmap_main.o
./build-local.sh obj/mpmap_trace.o
```

Then run `./build-local.sh` (no args) once to relink `bin/vg` when you're ready.

Any arguments you pass are forwarded to `make` as targets, e.g.
`./build-local.sh lib/libvg.a` or `./build-local.sh test`.

## Full clean rebuild

```bash
./build-local.sh clean          # make clean, then rebuild everything
```

`make clean` removes vg's objects and `bin/vg` (and its dependency libs), so this
triggers the full ~20–40 min build again. The local protobuf/abseil **toolchain is
not touched** by `make clean` — only `rm -rf /mnt/ssd/lalli/vg-latest-toolchain`
(followed by `./build-toolchain.sh`) rebuilds that.

## Why the wrapper is needed (what it sets, and why)

`build-local.sh` sets, for every `make` invocation:

| Setting | Reason |
|---|---|
| `PKG_CONFIG_PATH`/`PATH` → local toolchain first | use the local static protobuf + its matching `protoc`, not Homebrew's 33.4 |
| vg's `include/` + `lib/` prepended to `CPLUS_INCLUDE_PATH`/`LIBRARY_PATH`/`LDFLAGS` | Homebrew ships its own `sdsl`/`divsufsort` that otherwise shadow vg's vendored ones (gbwt then fails to link: `sdsl::simple_sds` undefined) |
| `CC=gcc-13` | Homebrew gcc-15 rejects the vendored elfutils (`-Werror=unterminated-string-initialization`); Ubuntu gcc-13 builds it |
| `CXX=g++-15 CXX_STANDARD=20` | abseil (a protobuf dependency) needs C++20's `std::*_ordering` under this libstdc++ |
| `--jobserver-style=pipe` | some dependency sub-makes can't read make 4.4's default *fifo* jobserver (`invalid --jobserver-auth 'fifo:...'`) |
| pre-creates `obj/*` and `lib/` dirs | under `-j` the compiler can race ahead of the Makefile's `mkdir` and fail writing `.d` files |

## Fork patches that make this compile

Small, deliberate changes vs. upstream v1.75.1 (kept minimal):

- **`Makefile`**: the `libbdsg` recipe pre-creates its object dir (`mkdir -p
  bdsg/obj lib bin`) — fixes a `-j` build race in that dependency.
- **`src/subcommand/inject_main.cpp`, `src/readfilter.hpp`,
  `src/minimizer_mapper_from_chains.cpp`**: qualify `vg::identity(...)` — v1.75.1's
  unqualified `identity` is ambiguous with C++20's `std::identity`.

## The mpmap splice-search instrumentation

This fork also carries the `vg mpmap --trace-splice-search FILE` instrumentation
(`src/mpmap_trace.{hpp,cpp}`, hooks in `multipath_mapper.cpp` / `mpmap_main.cpp`,
test `test/t/35_vg_mpmap_trace.t`). Build as above; run the test with:

```bash
cd test && prove -v t/35_vg_mpmap_trace.t
```
