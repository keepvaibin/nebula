#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The C++ library the Apple builds link (docs/APPLE.md, "Toolchain"): LLVM's libc++ and libc++abi as one static,
# hermetic archive for one platform and architecture, built with the same clang that builds Nebula. The system's
# libc++ follows the OS (a build for macOS 12 or iOS 15 would lose what a later one added) and the one in LLVM's
# macOS package is built for the packager's OS; this one has the deployment target Nebula has, and both modules
# (the host and the game) keep their own copy with nothing exported, as on Android.
#
#   build_libcxx.sh <llvm-source> <clang-root> <out> <platform> <arch> [<sdk path>]
#     platform: macos | ios | ios-simulator
#     arch:     x86_64 | arm64
set -eu

if [ $# -lt 5 ]; then
    sed -n '8,11p' "$0" >&2
    exit 2
fi
src=$1
llvm=$2
out=$3
platform=$4
arch=$5
sdk=${6:-}

case "$platform" in
macos)
    system_name=Darwin
    target=12.0
    [ -n "$sdk" ] || sdk=$(xcrun --sdk macosx --show-sdk-path)
    triple="$arch-apple-macosx$target"
    ;;
ios)
    system_name=iOS
    target=15.0
    [ -n "$sdk" ] || sdk=$(xcrun --sdk iphoneos --show-sdk-path)
    triple="$arch-apple-ios$target"
    ;;
ios-simulator)
    system_name=iOS
    target=15.0
    [ -n "$sdk" ] || sdk=$(xcrun --sdk iphonesimulator --show-sdk-path)
    triple="$arch-apple-ios$target-simulator"
    ;;
*)
    echo "unknown platform '$platform'" >&2
    exit 2
    ;;
esac

build="$out/build-$platform-$arch"
prefix="$out/$platform-$arch"
cmake=${CMAKE:-cmake}
ninja=${NINJA:-ninja}

"$cmake" -G Ninja -S "$src/runtimes" -B "$build" \
    -DCMAKE_MAKE_PROGRAM="$ninja" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_C_COMPILER="$llvm/bin/clang" \
    -DCMAKE_CXX_COMPILER="$llvm/bin/clang++" \
    -DCMAKE_AR="$llvm/bin/llvm-ar" \
    -DCMAKE_RANLIB="$llvm/bin/llvm-ranlib" \
    -DCMAKE_SYSTEM_NAME="$system_name" \
    -DCMAKE_OSX_SYSROOT="$sdk" \
    -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$target" \
    -DCMAKE_C_COMPILER_TARGET="$triple" \
    -DCMAKE_CXX_COMPILER_TARGET="$triple" \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi" \
    -DLLVM_INCLUDE_TESTS=OFF \
    -DLLVM_INCLUDE_DOCS=OFF \
    -DLIBCXX_ENABLE_SHARED=OFF \
    -DLIBCXX_ENABLE_STATIC=ON \
    -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON \
    -DLIBCXX_HERMETIC_STATIC_LIBRARY=ON \
    -DLIBCXX_INCLUDE_BENCHMARKS=OFF \
    -DLIBCXX_INCLUDE_TESTS=OFF \
    -DLIBCXX_CXX_ABI=libcxxabi \
    -DLIBCXXABI_ENABLE_SHARED=OFF \
    -DLIBCXXABI_ENABLE_STATIC=ON \
    -DLIBCXXABI_HERMETIC_STATIC_LIBRARY=ON \
    -DLIBCXXABI_INCLUDE_TESTS=OFF \
    -DLIBCXXABI_USE_LLVM_UNWINDER=OFF
"$cmake" --build "$build" --target install
echo "libc++ for $platform $arch: $prefix"
