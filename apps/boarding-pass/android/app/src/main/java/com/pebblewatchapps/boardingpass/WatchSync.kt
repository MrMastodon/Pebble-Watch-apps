package com.pebblewatchapps.boardingpass

/**
 * What the watch needs the moment the watchapp opens.
 *
 * This is the only chance the phone gets to push anything: an AppMessage only
 * reaches a watchapp that is in the foreground, so whatever the watch missed
 * while it was showing a watchface has to be worked out here.
 */
object WatchSync {

    sealed class Action {
        /** Push this pass, replacing whatever the watch has. */
        data class Send(val pass: EncodedPass) : Action()

        /** Tell the watch to forget the pass it still has. */
        data object Clear : Action()

        data object Nothing : Action()
    }

    fun pendingAction(store: PassStore): Action {
        val stored = store.load()

        if (stored != null) {
            // A stored pass wins over a pending deletion. The deletion can only
            // be left over from before this pass was imported, and wiping the
            // watch and then leaving it empty would be the worst of both.
            store.watchDeletePending = false

            val encoded = try {
                SymbolEncoder.encode(
                    stored.payload,
                    stored.format,
                    Bcbp.label(stored.payload) ?: DEFAULT_LABEL,
                )
            } catch (_: Exception) {
                // Nothing useful to do from a background push; the user gets a
                // real error message when they open the phone app.
                return Action.Nothing
            }

            // Showing a symbology the airline did not issue is the user's
            // decision, and a background push is not the place to take it.
            val agreed = store.substitutionAcknowledged || store.alwaysAllowSubstitution
            return if (encoded.isSubstituted && !agreed) Action.Nothing else Action.Send(encoded)
        }

        return if (store.watchDeletePending) Action.Clear else Action.Nothing
    }

    const val DEFAULT_LABEL = "Boarding pass"
}
