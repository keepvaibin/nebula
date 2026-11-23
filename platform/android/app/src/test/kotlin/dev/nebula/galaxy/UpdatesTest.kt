// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import android.content.pm.PackageInstaller
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.security.KeyPairGenerator
import java.security.Signature
import java.security.interfaces.RSAPublicKey
import java.util.Base64

/**
 * The app's update rules (Updates.kt) on the JVM: the version order and channels the game's updater keeps
 * (runtime/src/update/update_tests.cpp has the same cases), the release the listing offers, the notes as plain text,
 * the signature check and the installer's answers.
 */
class UpdatesTest {
    @Test
    fun versionsInTheUpdatersOrder() {
        assertTrue(Updates.compareVersions("2.0.10", "2.0.9") > 0)
        assertTrue(Updates.compareVersions("2.1.0", "2.1.0-beta.9") > 0)
        assertTrue(Updates.compareVersions("2.1.0-beta.10", "2.1.0-beta.9") > 0)
        assertEquals(0, Updates.compareVersions("2.0.1", "2.0.1"))
        assertTrue(Updates.compareVersions("", "0.0.0") < 0)
        assertTrue(Updates.compareVersions(null, "0.0.1") < 0)
        assertTrue(Updates.compareVersions("10.0.0", "9.9.9") > 0)
    }

    @Test
    fun channels() {
        assertTrue(Updates.isBeta("2.1.0-beta.3"))
        assertFalse(Updates.isBeta("2.1.0"))
        assertFalse(Updates.isBeta(null))
        assertEquals("Nebula-Beta-Android.apk", Updates.assetName("2.1.0-beta.3"))
        assertEquals("Nebula-Android.apk", Updates.assetName("2.1.0"))
    }

    private fun release(tag: String, asset: String, apkUrl: String?, sigUrl: String?, draft: Boolean = false, body: String? = "") =
        buildString {
            append("{\"tag_name\":\"").append(tag).append("\",\"draft\":").append(draft)
            append(",\"body\":").append(if (body == null) "null" else "\"" + body + "\"")
            append(",\"assets\":[")
            val items = mutableListOf<String>()
            if (apkUrl != null) items += "{\"name\":\"$asset\",\"browser_download_url\":\"$apkUrl\",\"size\":7}"
            if (sigUrl != null) items += "{\"name\":\"$asset.sig\",\"browser_download_url\":\"$sigUrl\"}"
            append(items.joinToString(",")).append("]}")
        }

    @Test
    fun theNewestOfTheChannelWithBothFilesOverHttps() {
        val apk = "Nebula-Android.apk"
        val beta = "Nebula-Beta-Android.apk"
        val json = listOf(
            release("v2.1.0", apk, "https://h/a", "https://h/s", draft = true),  // a draft
            release("v2.0.2", apk, "https://h/202", "https://h/202.sig", body = "Notes"),
            release("v2.0.3", apk, "http://h/203", "https://h/203.sig"),  // not over HTTPS
            release("v2.0.4", apk, "https://h/204", null),  // no signature
            release("v2.1.0-beta.1", beta, "https://h/b1", "https://h/b1.sig"),  // the other channel
            release("v1.9.0", apk, "https://h/190", "https://h/190.sig"),  // older
        ).joinToString(",", "[", "]")
        val offered = Updates.selectNewer(json, "2.0.0")
        assertEquals("2.0.2", offered?.version)
        assertEquals("v2.0.2", offered?.tag)
        assertEquals("https://h/202", offered?.apkUrl)
        assertEquals("https://h/202.sig", offered?.signatureUrl)
        assertEquals(7L, offered?.apkBytes)
        assertEquals("Notes", offered?.notes)
        assertNull("nothing newer than the newest", Updates.selectNewer(json, "2.0.2"))
        assertEquals("2.1.0-beta.1", Updates.selectNewer(json, "2.1.0-beta.0")?.version)
        assertNull("nothing newer on the beta channel", Updates.selectNewer(json, "2.1.0-beta.1"))
        assertNull("an empty listing", Updates.selectNewer("[]", "2.0.0"))
    }

    // The 2.0.0 beta (tag v2.0.0-beta.1): its player is later offered the true 2.0.0, a 1.0.2 player is not offered the beta.
    @Test
    fun theTwoZeroZeroBeta() {
        assertTrue(Updates.compareVersions("2.0.0", "2.0.0-beta.1") > 0)
        assertTrue(Updates.compareVersions("2.0.0-beta.1", "2.0.0") < 0)
        assertTrue(Updates.compareVersions("2.0.0-beta.2", "2.0.0-beta.1") > 0)
        assertTrue(Updates.compareVersions("2.0.0-beta.1", "1.0.2") > 0)
        assertTrue(Updates.isBeta("2.0.0-beta.1") && !Updates.isBeta("2.0.0"))
        assertEquals("Nebula-Beta-Android.apk", Updates.assetName("2.0.0-beta.1"))
        val json = listOf(
            release("v2.0.0-beta.1", "Nebula-Beta-Android.apk", "https://h/b1", "https://h/b1.sig"),
            release("v2.0.0", "Nebula-Android.apk", "https://h/200", "https://h/200.sig"),
        ).joinToString(",", "[", "]")
        assertEquals("2.0.0", Updates.selectNewer(json, "2.0.0-beta.1")?.version)
        assertEquals("2.0.0", Updates.selectNewer(json, "1.0.2")?.version)
        val onlyBeta = "[" + release("v2.0.0-beta.1", "Nebula-Beta-Android.apk", "https://h/b1", "https://h/b1.sig") + "]"
        assertNull("a 1.0.2 install is not offered the beta", Updates.selectNewer(onlyBeta, "1.0.2"))
        assertNull("a beta install is not offered itself", Updates.selectNewer(onlyBeta, "2.0.0-beta.1"))
        assertTrue(!Updates.takes("1.0.2", "2.0.0-beta.1") && Updates.takes("2.0.0-beta.1", "2.0.0"))
    }

    @Test
    fun aBetaInstallIsOfferedItsFinalRelease() {
        val json = listOf(
            release("v2.1.0-beta.1", "Nebula-Beta-Android.apk", "https://h/b1", "https://h/b1.sig"),
            release("v2.1.0", "Nebula-Android.apk", "https://h/210", "https://h/210.sig"),
            release("v2.2.0-beta.1", "Nebula-Android.apk", "https://h/x", "https://h/x.sig"),  // a beta under the release's name
        ).joinToString(",", "[", "]")
        val offered = Updates.selectNewer(json, "2.1.0-beta.1")
        assertEquals("2.1.0", offered?.version)
        assertEquals("https://h/210", offered?.apkUrl)
        assertEquals("2.1.0", Updates.selectNewer(json, "2.0.0")?.version)
        assertNull("a release install is not offered a beta", Updates.selectNewer(json, "2.1.0"))
        assertTrue(Updates.takes("2.1.0-beta.1", "2.1.0") && !Updates.takes("2.1.0", "2.2.0-beta.1"))
    }

    @Test
    fun notesAsPlainText() {
        val notes = Updates.plainNotes("# Title\r\n\r\n* item **bold** [link](https://x)\n> quote `code`\n\n---\n")
        assertEquals("Title\n\n- item bold link (https://x)\nquote code", notes)
        assertEquals("", Updates.plainNotes(null))
        val long = Updates.plainNotes("a".repeat(5000))
        assertEquals(4000 + 2, long.length)
        assertTrue(long.endsWith("\n…"))
    }

    @Test
    fun theSignatureIsTheKeysOverTheWholeFile() {
        val pair = KeyPairGenerator.getInstance("RSA").apply { initialize(2048) }.genKeyPair()
        val public = pair.public as RSAPublicKey
        val base64 = Base64.getEncoder()
        val modulus = base64.encodeToString(public.modulus.toByteArray())
        val exponent = base64.encodeToString(public.publicExponent.toByteArray())
        val file = File.createTempFile("nebula-update", ".apk")
        try {
            file.writeBytes(ByteArray(200_000) { (it * 31).toByte() })
            val signer = Signature.getInstance("SHA256withRSA").apply {
                initSign(pair.private)
                update(file.readBytes())
            }
            val signature = (base64.encodeToString(signer.sign()) + "\r\n").toByteArray(Charsets.US_ASCII)
            assertTrue("its own key's signature", Updates.verifySignature(file, signature, modulus, exponent))
            assertFalse("not the release key's", Updates.verifySignature(file, signature))
            assertFalse("garbage", Updates.verifySignature(file, "not base64 at all!".toByteArray(), modulus, exponent))
            assertFalse("empty", Updates.verifySignature(file, ByteArray(0), modulus, exponent))
            file.appendBytes(byteArrayOf(0))
            assertFalse("a byte more", Updates.verifySignature(file, signature, modulus, exponent))
        } finally {
            file.delete()
        }
    }

    @Test
    fun theInstallersAnswers() {
        assertNull(Updates.installFailure(PackageInstaller.STATUS_SUCCESS))
        assertEquals(UpdateFailure.Declined, Updates.installFailure(PackageInstaller.STATUS_FAILURE_ABORTED))
        assertEquals(UpdateFailure.Refused, Updates.installFailure(PackageInstaller.STATUS_FAILURE_CONFLICT))
        assertEquals(UpdateFailure.Refused, Updates.installFailure(PackageInstaller.STATUS_FAILURE_INCOMPATIBLE))
        assertEquals(UpdateFailure.Blocked, Updates.installFailure(PackageInstaller.STATUS_FAILURE_BLOCKED))
        assertEquals(UpdateFailure.NoRoom, Updates.installFailure(PackageInstaller.STATUS_FAILURE_STORAGE))
        assertEquals(UpdateFailure.InstallFailed, Updates.installFailure(PackageInstaller.STATUS_FAILURE))
    }
}
