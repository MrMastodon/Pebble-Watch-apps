package com.pebblewatchapps.boardingpass

import io.rebble.pebblekit2.client.BasePebbleListenerService
import io.rebble.pebblekit2.common.model.WatchIdentifier
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.UUID

/**
 * Catches the watch up whenever the watchapp is opened.
 *
 * The watch keeps its own copy and draws that first, so this is not what makes
 * the app work offline. It is what keeps the watch from showing a pass the
 * phone has since replaced or deleted - including when the phone app opened the
 * watchapp itself in order to send.
 */
class PebbleListenerService : BasePebbleListenerService() {

    // The base class would leave its scope running past onDestroy. Nothing here
    // should outlive the service: if the Pebble app has let go of us, whatever
    // we were pushing is no longer wanted.
    override val coroutineScope: CoroutineScope = MainScope()

    override fun onDestroy() {
        coroutineScope.cancel()
        super.onDestroy()
    }

    override fun onAppOpened(watchappUUID: UUID, watch: WatchIdentifier) {
        if (watchappUUID != PebbleLink.WATCHAPP_UUID) {
            return
        }
        coroutineScope.launch {
            val store = PassStore(applicationContext)
            // Decryption and encoding are off the main thread; the base class
            // runs these callbacks on it.
            when (val action = withContext(Dispatchers.IO) { WatchSync.pendingAction(store) }) {
                is WatchSync.Action.Send -> {
                    val sent = PebbleLink.send(applicationContext, action.pass).isSent
                    withContext(Dispatchers.IO) { store.deliveredToWatch = sent }
                }

                WatchSync.Action.Clear ->
                    if (PebbleLink.sendClear(applicationContext).isSent) {
                        withContext(Dispatchers.IO) { store.watchDeletePending = false }
                    }

                WatchSync.Action.Nothing -> Unit
            }
        }
    }
}
