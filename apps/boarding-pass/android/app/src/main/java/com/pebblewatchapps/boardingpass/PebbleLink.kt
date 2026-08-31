package com.pebblewatchapps.boardingpass

import android.content.Context
import io.rebble.pebblekit2.client.DefaultPebbleSender
import io.rebble.pebblekit2.common.model.PebbleDictionaryItem
import io.rebble.pebblekit2.common.model.TransmissionResult
import kotlinx.coroutines.delay
import java.util.UUID

/**
 * The phone half of the watch protocol: one AppMessage carrying a whole
 * boarding pass. Version 1.
 */
object PebbleLink {

    /** Must match the uuid in the watchapp's package.json. */
    val WATCHAPP_UUID: UUID = UUID.fromString("68df00cb-8d06-45c9-904a-389479abce52")

    private const val PROTOCOL_VERSION = 1

    private const val KEY_MODULES = 1u
    private const val KEY_MATRIX = 2u
    private const val KEY_LABEL = 3u
    private const val KEY_VERSION = 4u
    private const val KEY_CLEAR = 5u

    /** The watch label is a 32-byte buffer; leave room for the terminator. */
    private const val MAX_LABEL_LENGTH = 31

    sealed class Outcome {
        data object Sent : Outcome()

        /** Sent, but the watchapp had to be opened on the wrist first. */
        data object SentAfterOpeningWatchapp : Outcome()

        /** The Pebble app is not installed, or has not granted this app access. */
        data object NoPebbleApp : Outcome()

        data object NoWatchConnected : Outcome()

        data class Failed(val reason: String) : Outcome()

        val isSent: Boolean get() = this is Sent || this is SentAfterOpeningWatchapp
    }

    suspend fun send(context: Context, pass: EncodedPass): Outcome = send(
        context,
        mapOf(
            KEY_VERSION to PebbleDictionaryItem.UInt8(PROTOCOL_VERSION),
            KEY_MODULES to PebbleDictionaryItem.UInt8(pass.modules),
            KEY_MATRIX to PebbleDictionaryItem.Bytes(pass.packed),
            KEY_LABEL to PebbleDictionaryItem.Text(pass.label.take(MAX_LABEL_LENGTH)),
        ),
        openWatchapp = true,
    )

    /**
     * Tells the watch to forget its stored pass.
     *
     * An AppMessage only reaches a watchapp that is open, so this fails
     * whenever the user is not looking at it. The caller is expected to
     * remember that and try again when the watchapp next opens - a pass deleted
     * on the phone should not outlive it on the wrist.
     */
    suspend fun sendClear(context: Context): Outcome = send(
        context,
        mapOf(
            KEY_VERSION to PebbleDictionaryItem.UInt8(PROTOCOL_VERSION),
            KEY_CLEAR to PebbleDictionaryItem.UInt8(1),
        ),
        // Popping the watchapp onto someone's wrist only to tell it to forget
        // something is not worth it. The deletion waits for the next time they
        // open it themselves.
        openWatchapp = false,
    )

    private suspend fun send(
        context: Context,
        data: Map<UInt, PebbleDictionaryItem>,
        openWatchapp: Boolean,
    ): Outcome {
        val sender = DefaultPebbleSender(context.applicationContext)
        try {
            var results = sender.sendDataToPebble(WATCHAPP_UUID, data)
                ?: return Outcome.NoPebbleApp
            var opened = false

            // Only the watchapp in the foreground can receive an AppMessage, and
            // a watchface counts as a different app. Rather than telling the
            // user to go and open it, open it for them: they just asked for this
            // pass to be on the watch, so putting it in front of them is the
            // point rather than a surprise.
            if (openWatchapp && needsWatchappOpen(results)) {
                sender.startAppOnTheWatch(WATCHAPP_UUID) ?: return Outcome.NoPebbleApp
                opened = true
                var attempt = 0
                while (attempt < OPEN_RETRY_ATTEMPTS && needsWatchappOpen(results)) {
                    // The watchapp has to boot and open its inbox first.
                    delay(OPEN_RETRY_DELAY_MS)
                    results = sender.sendDataToPebble(WATCHAPP_UUID, data)
                        ?: return Outcome.NoPebbleApp
                    attempt++
                }
            }

            if (results.isEmpty()) {
                return Outcome.NoWatchConnected
            }
            // One success is enough: the pass is on a watch the user is wearing.
            if (results.values.any { it is TransmissionResult.Success }) {
                return if (opened) Outcome.SentAfterOpeningWatchapp else Outcome.Sent
            }
            return when (val result = results.values.first()) {
                TransmissionResult.FailedWatchNotConnected -> Outcome.NoWatchConnected
                TransmissionResult.FailedDifferentAppOpen ->
                    Outcome.Failed("open Boarding Pass on the watch first")
                TransmissionResult.FailedNoPermissions ->
                    Outcome.Failed("the watchapp does not list this phone app as its companion")
                TransmissionResult.FailedTimeout -> Outcome.Failed("the watch did not answer")
                TransmissionResult.FailedWatchNacked -> Outcome.Failed("the watch rejected the message")
                is TransmissionResult.Unknown -> Outcome.Failed(result.message ?: "unknown error")
                else -> Outcome.Failed(result.toString())
            }
        } finally {
            sender.close()
        }
    }

    /** The message bounced only because no watchapp of ours is in front. */
    private fun needsWatchappOpen(
        results: Map<io.rebble.pebblekit2.common.model.WatchIdentifier, TransmissionResult>,
    ): Boolean = results.isNotEmpty() &&
        results.values.none { it is TransmissionResult.Success } &&
        results.values.any { it == TransmissionResult.FailedDifferentAppOpen }

    private const val OPEN_RETRY_ATTEMPTS = 4
    private const val OPEN_RETRY_DELAY_MS = 700L
}
