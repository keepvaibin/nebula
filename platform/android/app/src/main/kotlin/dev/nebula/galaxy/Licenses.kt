// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

/** One component of the app and its license text in assets/licenses (staged by platform/android/build_apk.ps1). */
data class License(val name: String, val terms: String, val file: String)

val licenses = listOf(
    License("Nebula", "GPL-3.0-only", "GPL-3.0.txt"),
    License("Third-party notices", "Every project Nebula includes or adapts", "NOTICES.md"),
    License("Petari (Super Mario Galaxy decompilation)", "CC0-1.0", "Petari.txt"),
    License("SDL 3.4.18", "Zlib", "SDL.txt"),
    License("glslang 16.6.0", "BSD-3-Clause and others", "glslang.txt"),
    License("SPIRV-Tools", "Apache-2.0", "SPIRV-Tools.txt"),
    License("SPIRV-Headers", "MIT", "SPIRV-Headers.txt"),
    License("Vulkan-Headers 1.4.350.1", "Apache-2.0 OR MIT", "Vulkan-Headers.txt"),
    License("volk 1.4.350.1", "MIT", "volk.txt"),
    License("Vulkan Memory Allocator 3.4.0", "MIT", "VulkanMemoryAllocator.txt"),
    License("nod and 52 other Rust crates", "MIT, Apache-2.0, BSD, Zlib, Unlicense", "rust-dependencies-android.txt"),
    License("AndroidX and Jetpack Compose", "Apache-2.0", "Apache-2.0.txt"),
    License("Kotlin and kotlinx.coroutines", "Apache-2.0", "Apache-2.0.txt"),
)
