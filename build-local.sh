#!/usr/bin/env bash
#
# Build this vg fork locally on this machine.
#
# Why this wrapper exists: the system Homebrew has drifted (protobuf 33.4 +
# abseil that is C++17-only and ships *shared* libraries only, plus a Homebrew
# copy of sdsl/divsufsort that shadows vg's vendored versions). vg cannot be
# built directly against that. Instead we build against a work-directory-specific
# static protobuf + abseil toolchain (see build-toolchain.sh) and make vg's own
# vendored libraries authoritative on the include/library search paths.
#
# Usage:
#   ./build-local.sh                          # incremental build of bin/vg (only
#                                             #   recompiles sources you changed)
#   ./build-local.sh clean                    # make clean first, then full rebuild
#   ./build-local.sh obj/multipath_mapper.o   # build just one object (no relink)
#   ./build-local.sh clean bin/vg             # clean, then build a specific target
#
# Any arguments after an optional leading "clean" are passed straight to make as
# targets, so you can rebuild a single object without relinking, etc.
#
# Fastest iteration for splice-search work (recompile only the two changed objects
# then relink -- skips unrelated translation units):
#   ./build-local.sh obj/multipath_mapper.o obj/mpmap_trace.o obj/subcommand/mpmap_main.o
#   ./build-local.sh bin/vg
#
# Parallelism:
#   The JOBS env var controls -j (default: 64).  This machine has 256 cores, so
#   the default is fine; the project convention is JOBS=24 for lighter sessions:
#   JOBS=24 ./build-local.sh
#
# Prerequisite:
#   The first-ever build (or after deleting the toolchain dir) requires running
#   ./build-toolchain.sh once beforehand.  That clones protobuf v29.3 + its
#   bundled abseil, builds them static+PIC with g++-15, and installs to
#   $VG_TOOLCHAIN (default: /mnt/ssd/lalli/vg-latest-toolchain).  Runtime:
#   roughly 2 minutes at -j64.  build-local.sh checks for
#   $TOOLCHAIN/lib/libprotobuf.a and refuses to proceed if the toolchain is absent.
#
# Editor / LSP diagnostics:
#   clangd and other LSP servers typically lack the include paths that this build
#   uses (vendored sdsl, divsufsort, toolchain headers).  Errors like
#   "'algorithm' file not found" or "unknown type 'AbslAny'" appearing in the
#   editor are LSP noise -- they are NOT real build errors.  The authoritative
#   check is always a real ./build-local.sh compile.
#
set -euo pipefail

VGL="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Work-directory-specific protobuf(29.3)+abseil toolchain (static, PIC, C++20).
TOOLCHAIN="${VG_TOOLCHAIN:-/mnt/ssd/lalli/vg-latest-toolchain}"
# htslib's pkg-config metadata names bzip2 (which Ubuntu does not ship as a .pc
# file), while libvgio names jansson. Keep those pkg-config directories explicit
# and narrow so that adding the Homebrew prefix cannot make Homebrew's
# incompatible protobuf/SDSL packages authoritative. zlib and liblzma continue
# to resolve from the system pkg-config directories used to build libhts.
HOMEBREW_PREFIX="${VG_HOMEBREW_PREFIX:-/mnt/ssd/lalli/.linuxbrew}"
DEPENDENCY_PKG_CONFIG_PATH="$HOMEBREW_PREFIX/opt/bzip2/lib/pkgconfig:$HOMEBREW_PREFIX/opt/jansson/lib/pkgconfig"

if [ ! -f "$TOOLCHAIN/lib/libprotobuf.a" ]; then
    echo "ERROR: toolchain not found at $TOOLCHAIN (run build-toolchain.sh first)" >&2
    exit 1
fi

JOBS="${JOBS:-64}"

if [ "${1:-}" = "clean" ]; then
    make clean || true
    shift
fi

# Pre-create output dirs: with -j the compiler can race ahead of the Makefile's
# .pre-build mkdir and fail writing .d files into a not-yet-created obj/ subdir.
mkdir -p bin bin/unittest lib lib/pkgconfig include \
    obj obj/pic obj/algorithms obj/pic/algorithms obj/config obj/io obj/pic/io \
    obj/subcommand obj/unittest obj/unittest/support

# - PKG_CONFIG_PATH / PATH: use the local static protobuf + its protoc (matching
#   generated code) instead of Homebrew's protobuf 33.4.
# - CPPFLAGS / LDFLAGS: put vg's own include/ and lib/ FIRST so the vendored
#   sdsl/divsufsort win over Homebrew's shadowing copies. Do not put the same
#   path in CPLUS_INCLUDE_PATH: GCC treats that as a system directory and may
#   discard an earlier explicit -I as a duplicate, allowing /usr/local to win.
# - CC=gcc-13: build the C dependencies (elfutils) with Ubuntu gcc-13; Homebrew
#   gcc-15 rejects elfutils with -Werror=unterminated-string-initialization.
# - CXX=g++-15, CXX_STANDARD=20: abseil (a protobuf dependency) needs C++20 for
#   std::*_ordering under this libstdc++.
# - --jobserver-style=pipe: some dependency sub-makes cannot read make 4.4's
#   default fifo jobserver ("invalid --jobserver-auth 'fifo:...'").
# Ambient compiler variables on this host have historically contained colon-
# joined -L/-I options. Inheriting them makes configure tests select unrelated
# Homebrew libraries. Additional flags remain available through explicit
# VG_EXTRA_* variables, while the default build is deterministic.
CPPFLAGS="-I$VGL/include ${VG_EXTRA_CPPFLAGS:-}" \
CPLUS_INCLUDE_PATH="${VG_EXTRA_CPLUS_INCLUDE_PATH:-}" \
CPATH="${VG_EXTRA_CPATH:-}" \
LIBRARY_PATH="${VG_EXTRA_LIBRARY_PATH:-}" \
LDFLAGS="-L$VGL/lib -L$HOMEBREW_PREFIX/opt/jansson/lib -Wl,-rpath,$VGL/lib -Wl,-rpath,$HOMEBREW_PREFIX/opt/jansson/lib -Wl,--disable-new-dtags ${VG_EXTRA_LDFLAGS:-}" \
LD_LIBRARY_PATH="$VGL/lib${VG_EXTRA_LD_LIBRARY_PATH:+:$VG_EXTRA_LD_LIBRARY_PATH}" \
PKG_CONFIG_PATH="$TOOLCHAIN/lib/pkgconfig:$DEPENDENCY_PKG_CONFIG_PATH${VG_EXTRA_PKG_CONFIG_PATH:+:$VG_EXTRA_PKG_CONFIG_PATH}" \
PATH="$TOOLCHAIN/bin:$PATH" \
    make -j"$JOBS" --jobserver-style=pipe CC=gcc-13 CXX=g++-15 CXX_STANDARD=20 "$@"

echo "Build finished."
if [ -x "$VGL/bin/vg" ]; then
    "$VGL/bin/vg" version
fi
