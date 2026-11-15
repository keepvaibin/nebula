# SPDX-License-Identifier: GPL-3.0-only
# The Linux build's toolchain (platform/linux/build_linux.sh): clang and lld from the LLVM folder in the environment
# variable NEBULA_LLVM. lld links every program and library, because the system's GNU linkers read ThinLTO's bitcode
# only through LLVMgold.so, which LLVM's release archives do not include. As a toolchain file, it also applies to the
# small projects CMake builds to test the toolchain, such as the ThinLTO check (CheckIPOSupported).
if(NOT DEFINED ENV{NEBULA_LLVM})
    message(FATAL_ERROR "llvm-toolchain.cmake: set NEBULA_LLVM to the LLVM folder (the one holding bin/clang)")
endif()
set(CMAKE_C_COMPILER "$ENV{NEBULA_LLVM}/bin/clang")
set(CMAKE_CXX_COMPILER "$ENV{NEBULA_LLVM}/bin/clang++")
foreach(kind IN ITEMS EXE SHARED MODULE)
    set(CMAKE_${kind}_LINKER_FLAGS_INIT "-fuse-ld=lld")
endforeach()
