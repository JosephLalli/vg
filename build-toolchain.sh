#!/usr/bin/env bash
#
# Build the work-directory-specific static protobuf + abseil toolchain that
# build-local.sh uses. Installs to $VG_TOOLCHAIN (default:
# /mnt/ssd/lalli/vg-latest-toolchain). Fully local; no sudo; insulated from
# Homebrew drift.
#
set -euo pipefail
TOOLCHAIN="${VG_TOOLCHAIN:-/mnt/ssd/lalli/vg-latest-toolchain}"
WORK="${TMPDIR:-/tmp}/vg-protobuf-build"
JOBS="${JOBS:-64}"

rm -rf "$WORK" "$TOOLCHAIN"
mkdir -p "$WORK" && cd "$WORK"
# protobuf v29.3 pins a compatible abseil as a submodule; build both static.
git clone -b v29.3 --depth 1 --recurse-submodules -j8 https://github.com/protocolbuffers/protobuf.git
# Scrub Homebrew include paths so protobuf uses its own headers, not brew's 33.4.
env -u CPLUS_INCLUDE_PATH -u CPATH -u C_INCLUDE_PATH \
  cmake -S protobuf -B protobuf/build -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=20 \
    -DCMAKE_CXX_COMPILER=g++-15 \
    -Dprotobuf_ABSL_PROVIDER=module \
    -Dprotobuf_BUILD_TESTS=OFF \
    -DBUILD_SHARED_LIBS=OFF \
    -DABSL_PROPAGATE_CXX_STD=ON \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
env -u CPLUS_INCLUDE_PATH -u CPATH -u C_INCLUDE_PATH cmake --build protobuf/build -j"$JOBS"
cmake --install protobuf/build
echo "Toolchain installed to $TOOLCHAIN ($("$TOOLCHAIN"/bin/protoc --version))"
