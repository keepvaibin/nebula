#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Fetches the dependency sources the SDL builds take from one folder (NEBULA_DEPS: platform/sdl/CMakeLists.txt for
# Linux and Android, platform/apple/CMakeLists.txt for Apple's systems), at the versions and under the folder names
# those files expect. The Windows build fetches its own (cmake/vulkan_deps.cmake).
#
#   tools/fetch_build_deps.sh <folder> [android] [apple]
#
#   android  also libadrenotools with linkernsbypass (the Android app's Vulkan driver loader)
#   apple    also SPIRV-Cross and MoltenVK's macOS release
#
# A folder already there is kept: the script fetches only what is missing. curl and tar are taken from PATH.
set -eu

[ $# -ge 1 ] || { sed -n '7,12p' "$0" >&2; exit 2; }
deps=$1
shift
mkdir -p "$deps"
deps=$(cd "$deps" && pwd)

SDL=3.4.18
VULKAN_SDK=vulkan-sdk-1.4.350.1
GLSLANG=16.6.0
VMA=3.4.0
MOLTENVK=v1.4.2
ADRENOTOOLS=8fae8ce254dfc1344527e05301e43f37dea2df80
LINKERNSBYPASS=aa3975893d83ef1bc84c321ec60c65fbf1287887

# fetch <url> <folder the archive unpacks to> <folder it is kept as>
fetch() {
    # An empty folder is not a checkout (a source archive keeps a submodule's folder empty).
    if [ -d "$deps/$3" ] && [ -n "$(ls -A "$deps/$3")" ]; then
        return 0
    fi
    rm -rf "${deps:?}/$3"
    work=$(mktemp -d "$deps/.fetch.XXXXXX")
    echo "fetching $1"
    curl -fsSL --retry 3 "$1" -o "$work/archive.tar.gz"
    # Relative paths: on Windows, Git's tar reads "D:\..." as a remote host.
    (cd "$work" && tar -xzf archive.tar.gz)
    mkdir -p "$(dirname "$deps/$3")"
    mv "$work/$2" "$deps/$3"
    rm -rf "$work"
}

github=https://github.com
fetch "$github/libsdl-org/SDL/releases/download/release-$SDL/SDL3-$SDL.tar.gz" "SDL3-$SDL" "SDL3-$SDL"
fetch "$github/KhronosGroup/Vulkan-Headers/archive/refs/tags/$VULKAN_SDK.tar.gz" "Vulkan-Headers-$VULKAN_SDK" \
    "Vulkan-Headers-$VULKAN_SDK"
fetch "$github/zeux/volk/archive/refs/tags/$VULKAN_SDK.tar.gz" "volk-$VULKAN_SDK" "volk-$VULKAN_SDK"
fetch "$github/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v$VMA.tar.gz" \
    "VulkanMemoryAllocator-$VMA" "VulkanMemoryAllocator-v$VMA"
# glslang with the SPIR-V tools and headers of the same Vulkan SDK in the folders its build looks in.
fetch "$github/KhronosGroup/glslang/archive/refs/tags/$GLSLANG.tar.gz" "glslang-$GLSLANG" glslang-src
fetch "$github/KhronosGroup/SPIRV-Tools/archive/refs/tags/$VULKAN_SDK.tar.gz" "SPIRV-Tools-$VULKAN_SDK" \
    glslang-src/External/spirv-tools
fetch "$github/KhronosGroup/SPIRV-Headers/archive/refs/tags/$VULKAN_SDK.tar.gz" "SPIRV-Headers-$VULKAN_SDK" \
    glslang-src/External/spirv-tools/external/spirv-headers

for extra in "$@"; do
    case $extra in
    android)
        fetch "$github/bylaws/libadrenotools/archive/$ADRENOTOOLS.tar.gz" "libadrenotools-$ADRENOTOOLS" \
            "libadrenotools-$(echo "$ADRENOTOOLS" | cut -c1-7)"
        fetch "$github/bylaws/liblinkernsbypass/archive/$LINKERNSBYPASS.tar.gz" "liblinkernsbypass-$LINKERNSBYPASS" \
            "libadrenotools-$(echo "$ADRENOTOOLS" | cut -c1-7)/lib/linkernsbypass"
        ;;
    apple)
        fetch "$github/KhronosGroup/SPIRV-Cross/archive/refs/tags/$VULKAN_SDK.tar.gz" "SPIRV-Cross-$VULKAN_SDK" \
            "SPIRV-Cross-$VULKAN_SDK"
        if [ ! -d "$deps/MoltenVK" ]; then
            echo "fetching MoltenVK $MOLTENVK"
            mkdir -p "$deps/MoltenVK"
            curl -fsSL --retry 3 "$github/KhronosGroup/MoltenVK/releases/download/$MOLTENVK/MoltenVK-macos.tar" |
                tar -x -C "$deps/MoltenVK"
        fi
        ;;
    *)
        echo "fetch_build_deps.sh: unknown extra $extra (android or apple)" >&2
        exit 2
        ;;
    esac
done
echo "$deps"
