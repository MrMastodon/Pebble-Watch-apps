package com.pebblewatchapps.boardingpass

import androidx.test.core.app.ApplicationProvider
import com.google.zxing.BarcodeFormat
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/**
 * What the phone pushes when the watchapp opens. This is the only moment it can
 * push anything, so getting the precedence wrong strands the watch showing
 * something the phone no longer has.
 */
@RunWith(RobolectricTestRunner::class)
class WatchSyncTest {

    private class InMemoryPassKey : PassKey {
        private var key: SecretKey = generate()
        override fun secretKey(): SecretKey = key
        override fun discard() { key = generate() }
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
    fun `sends the stored pass`() {
        store.save(SYNTHETIC_BCBP, BarcodeFormat.AZTEC)

        val action = WatchSync.pendingAction(store)

        assertTrue("got $action", action is WatchSync.Action.Send)
        assertEquals(BarcodeFormat.AZTEC, (action as WatchSync.Action.Send).pass.format)
    }

    @Test
    fun `clears the watch when the pass was deleted while it was out of reach`() {
        store.clear()
        store.watchDeletePending = true

        assertEquals(WatchSync.Action.Clear, WatchSync.pendingAction(store))
    }

    @Test
    fun `a stored pass beats a stale deletion, and cancels it`() {
        // Wiping the watch and then leaving it empty would be the worst of both.
        store.save(SYNTHETIC_BCBP, BarcodeFormat.AZTEC)
        store.watchDeletePending = true

        val action = WatchSync.pendingAction(store)

        assertTrue("got $action", action is WatchSync.Action.Send)
        assertFalse("the stale deletion should be gone", store.watchDeletePending)
    }

    @Test
    fun `does nothing when there is nothing to say`() {
        assertEquals(WatchSync.Action.Nothing, WatchSync.pendingAction(store))
    }

    @Test
    fun `will not silently substitute a symbology in the background`() {
        // PDF417 cannot be drawn, so this pass can only go across as Aztec -
        // which is a decision for the user, not for a background push.
        store.save(SYNTHETIC_BCBP, BarcodeFormat.PDF_417)

        assertEquals(WatchSync.Action.Nothing, WatchSync.pendingAction(store))
    }

    @Test
    fun `sends a substituted pass once the user has agreed to it`() {
        store.save(SYNTHETIC_BCBP, BarcodeFormat.PDF_417)
        store.substitutionAcknowledged = true

        val action = WatchSync.pendingAction(store)

        assertTrue("got $action", action is WatchSync.Action.Send)
        assertEquals(BarcodeFormat.AZTEC, (action as WatchSync.Action.Send).pass.format)
    }

    private companion object {
        // Synthetic, invented values. Real BCBP data must never be committed.
        const val SYNTHETIC_BCBP =
            "M1TESTER/SYNTHETIC    EZZ9XY9 OSLCPHSK 4174 250Y012A0034 147>50B0WW5180BSK 2A117000000000000SK SK 0000000000000000 20KNSYNTHETIC   "
    }
}
