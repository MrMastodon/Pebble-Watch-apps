package com.pebblewatchapps.boardingpass

import androidx.test.core.app.ApplicationProvider
import com.google.zxing.BarcodeFormat
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class PassStoreTest {

    /**
     * A plain in-memory AES key. The Android Keystore does not exist off a
     * device, so the real PassKey cannot run here; everything else in PassStore
     * can, and it is where the logic worth testing lives.
     */
    private class InMemoryPassKey : PassKey {
        private var key: SecretKey = generate()

        override fun secretKey(): SecretKey = key

        override fun discard() {
            key = generate()
        }

        private fun generate(): SecretKey =
            KeyGenerator.getInstance("AES").apply { init(256) }.generateKey()
    }

    private lateinit var store: PassStore

    @Before
    fun setUp() {
        store = PassStore(ApplicationProvider.getApplicationContext(), InMemoryPassKey())
        store.clear()
        store.alwaysAllowSubstitution = false
        store.watchDeletePending = false
    }

    @Test
    fun `a newly imported pass is not on the watch yet`() {
        store.save(SYNTHETIC_BCBP, BarcodeFormat.AZTEC)
        store.deliveredToWatch = true

        // Replacing the pass means the watch is showing the old one.
        store.save(SYNTHETIC_BCBP, BarcodeFormat.QR_CODE)

        assertFalse(store.deliveredToWatch)
    }

    @Test
    fun `deleting the pass forgets that the watch had it`() {
        store.save(SYNTHETIC_BCBP, BarcodeFormat.AZTEC)
        store.deliveredToWatch = true

        store.clear()

        assertFalse(store.deliveredToWatch)
    }

    @Test
    fun `a stored pass comes back with its symbology`() {
        store.save(SYNTHETIC_BCBP, BarcodeFormat.PDF_417)

        val stored = store.load()

        assertEquals(SYNTHETIC_BCBP, stored?.payload)
        assertEquals(BarcodeFormat.PDF_417, stored?.format)
    }

    @Test
    fun `clearing leaves nothing behind`() {
        store.save(SYNTHETIC_BCBP, BarcodeFormat.AZTEC)

        store.clear()

        assertNull(store.load())
    }

    @Test
    fun `a pending watch deletion survives clearing the pass`() {
        // The order the app actually uses: wipe the pass, then record that the
        // watch has not been told. If clear() dropped the flag, a pass deleted
        // while the watch was out of reach would stay on the wrist for good.
        store.save(SYNTHETIC_BCBP, BarcodeFormat.AZTEC)
        store.clear()
        store.watchDeletePending = true

        assertTrue(store.watchDeletePending)
    }

    @Test
    fun `a new pass cancels a deletion the watch never heard about`() {
        store.watchDeletePending = true

        store.save(SYNTHETIC_BCBP, BarcodeFormat.AZTEC)

        assertFalse(store.watchDeletePending)
    }

    @Test
    fun `agreeing to a substitution does not carry over to the next pass`() {
        store.save(SYNTHETIC_BCBP, BarcodeFormat.PDF_417)
        store.substitutionAcknowledged = true

        store.save(SYNTHETIC_BCBP, BarcodeFormat.PDF_417)

        assertFalse(store.substitutionAcknowledged)
    }

    @Test
    fun `do not ask again outlives a single pass`() {
        store.alwaysAllowSubstitution = true

        store.save(SYNTHETIC_BCBP, BarcodeFormat.PDF_417)
        store.clear()

        assertTrue(store.alwaysAllowSubstitution)
    }

    private companion object {
        // Synthetic, invented values. Real BCBP data must never be committed.
        const val SYNTHETIC_BCBP =
            "M1TESTER/SYNTHETIC    EZZ9XY9 OSLCPHSK 4174 250Y012A0034 147>50B0WW5180BSK 2A117000000000000SK SK 0000000000000000 20KNSYNTHETIC   "
    }
}
