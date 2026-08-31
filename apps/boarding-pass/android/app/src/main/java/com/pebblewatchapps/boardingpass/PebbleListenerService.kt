package com.pebblewatchapps.boardingpass

import io.rebble.pebblekit2.client.BasePebbleListenerService
import io.rebble.pebblekit2.common.model.WatchIdentifier
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.UUID

/**
 * Pushes the stored boarding pass whenever the watchapp is opened.
 *
 * The watch keeps its own copy and draws that first, so this is not what makes
 * the app work offline - it is what keeps the watch from ever showing a stale
 * pass after the user has shared a newer screenshot.
 */
class PebbleListenerService : BasePebbleListenerService() {

    override fun onAppOpened(watchappUUID: UUID, watch: WatchIdentifier) {
        if (watchappUUID != PebbleLink.WATCHAPP_UUID) {
            return
        }
        coroutineScope.launch {
            // Decryption and encoding are off the main thread; the base class
            // runs these callbacks on it.
            val pass = withContext(Dispatchers.IO) {
                val store = PassStore(applicationContext)
                val stored = store.load() ?: return@withContext null
                val encoded = try {
                    SymbolEncoder.encode(
                        stored.payload,
                        stored.format,
                        Bcbp.label(stored.payload) ?: DEFAULT_LABEL,
                    )
                } catch (_: Exception) {
                    // Nothing useful to do from a background push; the user gets
                    // a real error message when they open the phone app.
                    return@withContext null
                }
                // Showing a symbology the airline did not issue is the user's
                // decision, and a background push is not the place to take it.
                val agreed = store.substitutionAcknowledged || store.alwaysAllowSubstitution
                if (encoded.isSubstituted && !agreed) null else encoded
            } ?: return@launch

            PebbleLink.send(applicationContext, pass)
        }
    }

    private companion object {
        const val DEFAULT_LABEL = "Boarding pass"
    }
}
