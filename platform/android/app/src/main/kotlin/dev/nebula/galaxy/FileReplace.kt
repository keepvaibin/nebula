// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.StandardCopyOption

/**
 * [from] in [to]'s place in one step, a file there replaced (rename(2) on Android; MoveFileEx where the JVM tests run on
 * Windows, whose File.renameTo replaces nothing). False: nothing moved.
 */
internal fun replaceFile(from: File, to: File): Boolean = try {
    Files.move(from.toPath(), to.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
    true
} catch (e: IOException) {
    false
}
