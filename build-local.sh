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
#   ./build-local.sh                       # incremental build of bin/vg (only
#                                          #   recompiles sources you changed)
#   ./build-local.sh clean                 # make clean first, then full rebuild
#   ./build-local.sh obj/multipath_mapper.o  # build just one object
#   ./build-local.sh clean bin/vg          # clean, then build a specific target
#
# Any arguments after an optional leading "clean" are passed straight to make as
# targets, so you can rebuild a single object without relinking, etc.
#
set -euo pipefail

VGL="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Work-directory-specific protobuf(29.3)+abseil toolchain (static, PIC, C++20).
TOOLCHAIN="${VG_TOOLCHAIN:-/mnt/ssd/lalli/vg-latest-toolchain}"

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
# - CPLUS_INCLUDE_PATH / LIBRARY_PATH / LDFLAGS: put vg's own include/ and lib/
#   FIRST so the vendored sdsl/divsufsort win over Homebrew's shadowing copies.
# - CC=gcc-13: build the C dependencies (elfutils) with Ubuntu gcc-13; Homebrew
#   gcc-15 rejects elfutils with -Werror=unterminated-string-initialization.
# - CXX=g++-15, CXX_STANDARD=20: abseil (a protobuf dependency) needs C++20 for
#   std::*_ordering under this libstdc++.
# - --jobserver-style=pipe: some dependency sub-makes cannot read make 4.4's
#   default fifo jobserver ("invalid --jobserver-auth 'fifo:...'").
CPLUS_INCLUDE_PATH="$VGL/include:${CPLUS_INCLUDE_PATH:-}" \
LIBRARY_PATH="$VGL/lib:${LIBRARY_PATH:-}" \
LDFLAGS="-L$VGL/lib ${LDFLAGS:-}" \
PKG_CONFIG_PATH="$TOOLCHAIN/lib/pkgconfig:${PKG_CONFIG_PATH:-}" \
PATH="$TOOLCHAIN/bin:$PATH" \
    make -j"$JOBS" --jobserver-style=pipe CC=gcc-13 CXX=g++-15 CXX_STANDARD=20 "$@"

echo "Build finished."
[ -x "$VGL/bin/vg" ] && "$VGL/bin/vg" version
