// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource

/** A product's own name or a license's identifier, shown as it is, or a row of the text table with its arguments. */
class Words private constructor(private val literal: String?, @StringRes private val id: Int, private val args: Array<out Any>) {
    @Composable
    fun text(): String = literal ?: if (args.isEmpty()) stringResource(id) else stringResource(id, *args)

    companion object {
        fun of(literal: String) = Words(literal, 0, emptyArray())

        fun row(@StringRes id: Int, vararg args: Any) = Words(null, id, args)
    }
}

/** One component of the app and its license text in assets/licenses (staged by platform/android/build_apk.ps1). */
data class License(val name: Words, val terms: Words, val file: String)

private fun license(name: String, terms: String, file: String) = License(Words.of(name), Words.of(terms), file)

val licenses = listOf(
    license("Nebula", "GPL-3.0-only", "GPL-3.0.txt"),
    License(Words.row(R.string.LicenseNotices), Words.row(R.string.LicenseNoticesTerms), "NOTICES.md"),
    License(Words.row(R.string.LicensePetari), Words.of("CC0-1.0"), "Petari.txt"),
    license("SDL 3.4.18", "Zlib", "SDL.txt"),
    License(Words.of("glslang 16.6.0"), Words.row(R.string.LicenseAndOthers, "BSD-3-Clause"), "glslang.txt"),
    license("SPIRV-Tools", "Apache-2.0", "SPIRV-Tools.txt"),
    license("SPIRV-Headers", "MIT", "SPIRV-Headers.txt"),
    license("Vulkan-Headers 1.4.350.1", "Apache-2.0 OR MIT", "Vulkan-Headers.txt"),
    license("volk 1.4.350.1", "MIT", "volk.txt"),
    license("Vulkan Memory Allocator 3.4.0", "MIT", "VulkanMemoryAllocator.txt"),
    license("libarchive 3.8.1", "BSD-2-Clause", "libarchive.txt"),
    license("xz 5.8.1 (liblzma)", "0BSD", "xz.txt"),
    license("zlib 1.3.1", "Zlib", "zlib.txt"),
    License(Words.row(R.string.LicenseRust, 52), Words.of("MIT, Apache-2.0, BSD, Zlib, Unlicense"), "rust-dependencies-android.txt"),
    License(Words.row(R.string.LicensePair, "AndroidX", "Jetpack Compose"), Words.of("Apache-2.0"), "Apache-2.0.txt"),
    License(Words.row(R.string.LicensePair, "Kotlin", "kotlinx.coroutines"), Words.of("Apache-2.0"), "Apache-2.0.txt"),
)
