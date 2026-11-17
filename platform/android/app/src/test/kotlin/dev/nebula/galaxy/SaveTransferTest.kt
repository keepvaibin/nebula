// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.InputStream

/** SaveTransfer's bounded read: a whole stream up to the limit, none past it, however the stream hands its bytes over. */
class SaveTransferTest {
    // A stream that hands over at most `step` bytes a read, as a slow document provider does.
    private class Trickle(private val bytes: ByteArray, private val step: Int) : InputStream() {
        private var at = 0
        override fun read(): Int = if (at < bytes.size) bytes[at++].toInt() and 0xFF else -1
        override fun read(b: ByteArray, off: Int, len: Int): Int {
            if (at >= bytes.size) return -1
            val n = minOf(len, step, bytes.size - at)
            System.arraycopy(bytes, at, b, off, n)
            at += n
            return n
        }
    }

    @Test
    fun aWholeStreamUpToTheLimit() {
        val bytes = ByteArray(200_000) { it.toByte() }
        assertArrayEquals(bytes, SaveTransfer.readAtMost(ByteArrayInputStream(bytes), 200_000))
        assertArrayEquals(bytes, SaveTransfer.readAtMost(Trickle(bytes, 7), 200_000))
        assertEquals(0, SaveTransfer.readAtMost(ByteArrayInputStream(ByteArray(0)), 0)?.size)
    }

    @Test
    fun nonePastIt() {
        val bytes = ByteArray(200_001)
        assertNull(SaveTransfer.readAtMost(ByteArrayInputStream(bytes), 200_000))
        assertNull(SaveTransfer.readAtMost(Trickle(bytes, 1000), 200_000))
        assertNull(SaveTransfer.readAtMost(ByteArrayInputStream(ByteArray(1)), 0))
    }
}
