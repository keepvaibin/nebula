#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Builds Nebula for Linux into the program folder installer/package-linux.sh packs: the SDL host with the Vulkan
# renderer (nebula, libSDL3.so), the game module with and without the function rows (libsmg_game.so,
# libsmg_game_plain.so) and its self-description, and the game importer (nebula-import, libnebula_import.so).
#
#   platform/linux/build_linux.sh -d <deps> -l <llvm> -w <work folder> [-s <FSR 3 shaders>] [-j <jobs>] [-n]
#
#   <deps>   the dependency checkouts platform/sdl/CMakeLists.txt names (SDL3, glslang-src, Vulkan-Headers, volk,
#            VulkanMemoryAllocator)
#   <llvm>   LLVM 21 or later, whose clang and lld build everything (platform/linux/llvm-toolchain.cmake): the game
#            module's row generator needs its libclang
#   -s       FidelityFX's Vulkan shader headers, made on Windows (cmake/ffx-vk-shaders, the workflows' step
#            .github/actions/ffx-vk-shaders): with them the program holds AMD's FSR 3. Without them it does not, and says so once.
#            A build that makes a package (the environment variable NEBULA_REQUIRE_FSR3=ON, as release.yml and check.yml set it)
#            stops without them.
#   -j       how many compiles run at once (default: the build tool's own choice)
#   -n       a build with no mod support (CMake NEBULA_MODS=OFF; the environment variable NEBULA_MODS=OFF does the same): only
#            the plain game module (libsmg_game_plain.so), no function rows, no self-description
#
# The program folder is <work>/bin. The host and the game module are the players' builds, with ThinLTO
# (platform/sdl/CMakeLists.txt NEBULA_HOST_IPO, native/CMakeLists.txt NEBULA_LTO).
# cmake, ninja, python3 and cargo are taken from PATH.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
deps=
llvm=
work=
jobs=
shaders=
nomods=
[ "${NEBULA_MODS:-}" = OFF ] && nomods=1
while getopts "d:l:w:s:j:n" option; do
    case "$option" in
    d) deps=$OPTARG ;;
    l) llvm=$OPTARG ;;
    w) work=$OPTARG ;;
    s) shaders=$OPTARG ;;
    j) jobs="--parallel $OPTARG" ;;
    n) nomods=1 ;;
    *) sed -n '7,19p' "$0" >&2; exit 2 ;;
    esac
done
if [ -z "$deps" ] || [ -z "$llvm" ] || [ -z "$work" ]; then
    sed -n '7,19p' "$0" >&2
    exit 2
fi
# Every CMake project reads this (cmake/nebula_options.cmake).
if [ -n "$nomods" ]; then NEBULA_MODS=OFF; export NEBULA_MODS; fi
for tool in clang clang++ ld.lld; do
    [ -x "$llvm/bin/$tool" ] || { echo "build_linux.sh: no $tool in $llvm/bin" >&2; exit 2; }
done
# FSR 3 (cmake/fidelityfx_android.cmake): its shader headers come from the Windows step of the workflow's run.
case "${NEBULA_REQUIRE_FSR3:-}" in
ON | on | 1 | TRUE | true | YES | yes | Y | y) require_fsr3=1 ;;
*) require_fsr3= ;;
esac
if [ -n "$shaders" ] && [ ! -f "$shaders/nebula-luma16.txt" ]; then
    echo "build_linux.sh: $shaders holds no nebula-luma16.txt: it is not FidelityFX's shader folder (cmake/ffx-vk-shaders)" >&2
    exit 2
fi
if [ -z "$shaders" ] && [ -n "$require_fsr3" ]; then
    echo "build_linux.sh: this build makes a package, which holds AMD's FSR 3, and no -s <FidelityFX shader headers> was given (they are made on Windows: .github/actions/ffx-vk-shaders)" >&2
    exit 2
fi
mkdir -p "$work"
deps=$(cd "$deps" && pwd)
[ -z "$shaders" ] || shaders=$(cd "$shaders" && pwd)
llvm=$(cd "$llvm" && pwd)
work=$(cd "$work" && pwd)

# Every platform's release items (permissions, usage texts, file and link associations) are in the tree.
python3 "$root/tools/check_platform_items.py" "$root"

export NEBULA_LLVM="$llvm"
toolchain="-DCMAKE_TOOLCHAIN_FILE=$here/llvm-toolchain.cmake"

# The host: the SDL program, which makes its shader tools first (platform/sdl/CMakeLists.txt NEBULA_HOST_TOOLS).
# shellcheck disable=SC2086
cmake -S "$root/platform/sdl" -B "$work/host" -G Ninja -DCMAKE_BUILD_TYPE=Release $toolchain "-DNEBULA_DEPS=$deps" ${shaders:+"-DNEBULA_FFX_VK_SHADERS=$shaders"}
# shellcheck disable=SC2086
cmake --build "$work/host" --target main $jobs

# The game module: both builds of it, the self-description and the services list (no mod support: the plain build only).
# shellcheck disable=SC2086
cmake -S "$root/native" -B "$work/game" -G Ninja -DCMAKE_BUILD_TYPE=Release $toolchain -DNEBULA_LTO=ON
if [ -n "$nomods" ]; then
    # shellcheck disable=SC2086
    cmake --build "$work/game" --target smg_game_plain $jobs
else
    # shellcheck disable=SC2086
    cmake --build "$work/game" --target smg_game smg_game_plain luma_self_description luma_services_file $jobs
fi

# The importer: the program Setup's import runs and the library the game cooks mods' files with. The source paths it
# keeps (panic texts) name no build machine's folders.
cargo_home=${CARGO_HOME:-$HOME/.cargo}
rustup_home=${RUSTUP_HOME:-$HOME/.rustup}
(
    cd "$root"
    export CARGO_TARGET_DIR="$work/cargo"
    export RUSTFLAGS="--remap-path-prefix=$cargo_home=cargo --remap-path-prefix=$rustup_home=rustup --remap-path-prefix=$root=nebula"
    cargo build --profile dist --locked -p nebula-import --bin nebula-import --lib
)

# The program folder, as installer/package-linux.sh reads it.
bin="$work/bin"
rm -rf "$bin"
mkdir -p "$bin/luma"
cp "$work/host/nebula" "$bin/nebula"
# SDL's library is built beside the program (platform/sdl/CMakeLists.txt), which finds it in its own folder.
[ -e "$work/host/libSDL3.so.0" ] || { echo "build_linux.sh: the host build has no libSDL3.so.0" >&2; exit 1; }
cp -P "$work/host"/libSDL3.so* "$bin/"
cp "$work/game/libsmg_game_plain.so" "$bin/"
cp "$work/cargo/dist/nebula-import" "$work/cargo/dist/libnebula_import.so" "$bin/"
if [ -z "$nomods" ]; then
    cp "$work/game/libsmg_game.so" "$bin/"
    cp -R "$work/game/luma/." "$bin/luma/"
    for file in luma_types/luma_types.json luma_types/luma_functions.json luma_types/luma_numbers.json \
        luma_types/luma_services.json smg_game.luma-placement.json smg_game.luma-self.json; do
        cp "$work/game/$file" "$bin/luma/"
    done
fi
echo "$bin"
