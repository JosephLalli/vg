# Building this vg fork on this machine

**Always build with `./build-local.sh` — never a bare `make`.** The system
Homebrew has drifted (protobuf 33.4 + a C++17-only, *shared-only* abseil, plus a
Homebrew `sdsl`/`divsufsort` that shadow vg's vendored copies), so a plain `make`
fails. `build-local.sh` bakes in the environment that makes vg build here; it just
calls `make` with the right settings, so all normal `make` behaviour (incremental
compilation, single targets, `make clean`) works through it.

## TL;DR

```bash
./build-toolchain.sh      # ONCE: builds the local static protobuf+abseil toolchain (~2 min at -j64)
./build-local.sh          # build bin/vg (incremental: only recompiles what changed)
```

## First-time setup

1. `./build-toolchain.sh`
   Builds a work-directory-specific **static** protobuf (v29.3) + abseil into
   `/mnt/ssd/lalli/vg-latest-toolchain` (override with `VG_TOOLCHAIN=/path`). This
   is what replaces the unusable Homebrew protobuf/abseil. You only ever run this
   once (or after `rm -rf` of the toolchain).
   Runtime: ~2 min at `-j64` on this 256-core machine.
   `build-local.sh` checks for `$TOOLCHAIN/lib/libprotobuf.a` on every invocation
   and exits with a clear error if the toolchain is absent.

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

### Splice-search iteration pattern

When iterating on `--trace-splice-search` specifically, the three changed objects
are `multipath_mapper.o`, `mpmap_trace.o`, and `mpmap_main.o`.  Compile all three
then relink in two steps (skipping the rest of libvg):

```bash
./build-local.sh obj/multipath_mapper.o obj/mpmap_trace.o obj/subcommand/mpmap_main.o
./build-local.sh bin/vg
```

This takes ~compile time for those three files (each a couple of minutes) plus the
relink (~1–2 min), rather than the full incremental make scan.

## Parallelism

`build-local.sh` defaults to `JOBS=64` (the `-j` value passed to `make`).  This
machine has 256 cores, so 64 is conservative and safe.  To use more or fewer:

```bash
JOBS=128 ./build-local.sh       # more aggressive
JOBS=24 ./build-local.sh        # lighter load, project-convention default for shared sessions
```

## Full clean rebuild

```bash
./build-local.sh clean          # make clean, then rebuild everything
```

`make clean` removes vg's objects and `bin/vg` (and its dependency libs), so this
triggers the full ~20–40 min build again. The local protobuf/abseil **toolchain is
not touched** by `make clean` — only `rm -rf /mnt/ssd/lalli/vg-latest-toolchain`
(followed by `./build-toolchain.sh`) rebuilds that.

## Editor / LSP diagnostics are not authoritative

clangd and other language servers typically do not see the vendored include paths or
the toolchain headers that `build-local.sh` injects.  Red squiggles such as
`'algorithm' file not found`, `unknown type 'AbslAny'`, or `no member named 'X' in
namespace 'std'` inside the editor are LSP noise — they reflect the LSP's missing
context, not real build errors.  The only authoritative check is a real
`./build-local.sh` compile.

## Why the wrapper is needed (what it sets, and why)

`build-local.sh` sets, for every `make` invocation:

| Setting | Reason |
|---|---|
| `PKG_CONFIG_PATH`/`PATH` → local toolchain first | use the local static protobuf + its matching `protoc`, not Homebrew's 33.4 |
| narrow Homebrew `bzip2`/`jansson` `pkgconfig` paths after the toolchain | satisfy htslib/libvgio dependencies without exposing Homebrew's protobuf/SDSL metadata; `zlib` and `liblzma` remain the system versions used by the local libhts build |
| explicit `CPPFLAGS=-I$checkout/include` | selects vg's vendored SDSL headers before incompatible Homebrew copies; the path is intentionally *not* put in `CPLUS_INCLUDE_PATH`, because GCC demotes duplicate include paths to system-header order |
| explicit checkout/Jansson `-L` and legacy `RPATH` arguments | selects checkout libraries first and the same Homebrew Jansson used for compilation at runtime; legacy `RPATH` cannot be silently preceded by an ambient `LD_LIBRARY_PATH` entry |
| empty default `CPLUS_INCLUDE_PATH`, `CPATH`, and `LIBRARY_PATH` | prevents malformed or stale shell settings from changing dependency selection; opt in to additions with the corresponding `VG_EXTRA_*` variables |
| `CC=gcc-13` | Homebrew gcc-15 rejects the vendored elfutils (`-Werror=unterminated-string-initialization`); the Ubuntu system `gcc-13` (at `/usr/bin/gcc-13`, v13.3.0) builds it cleanly |
| `CXX=g++-15 CXX_STANDARD=20` | abseil (a protobuf dependency) needs C++20's `std::*_ordering` under this libstdc++; `g++-15` is Homebrew GCC 15.2.0 at `/mnt/ssd/lalli/.linuxbrew/bin/g++-15` |
| `--jobserver-style=pipe` | some dependency sub-makes can't read make 4.4's default *fifo* jobserver (`invalid --jobserver-auth 'fifo:...'`) |
| pre-creates `obj/*` and `lib/` dirs | under `-j` the compiler can race ahead of the Makefile's `mkdir` and fail writing `.d` files |

The GCSA2 sub-build also passes its local library selection explicitly on every
link command:

```text
-L<checkout>/lib -Wl,-rpath,<checkout>/lib -Wl,--disable-new-dtags \
  -lsdsl -ldivsufsort -ldivsufsort64
```

Use `VG_EXTRA_CPPFLAGS`, `VG_EXTRA_LDFLAGS`,
`VG_EXTRA_CPLUS_INCLUDE_PATH`, `VG_EXTRA_CPATH`,
`VG_EXTRA_LIBRARY_PATH`, `VG_EXTRA_LD_LIBRARY_PATH`, or
`VG_EXTRA_PKG_CONFIG_PATH` only when an additional dependency is intentional.
Ambient values of the standard compiler variables are not inherited by the
wrapper.

## Fork patches that make this compile

Small, deliberate changes vs. upstream v1.75.1 (kept minimal):

- **`Makefile`**: the `libbdsg` recipe pre-creates its object dir (`mkdir -p
  bdsg/obj lib bin`) — fixes a `-j` build race in that dependency.
- **`Makefile`**: the vendored sparsehash configure/build step uses C++17 — its
  legacy self-tests call `std::allocator::rebind`, which was removed in C++20;
  `vg` itself and the protobuf/abseil toolchain still compile as C++20.
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
