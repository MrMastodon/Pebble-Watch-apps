package com.pebblewatchapps.boardingpass

import android.content.Context
import io.rebble.pebblekit2.client.DefaultPebbleSender
import io.rebble.pebblekit2.common.model.PebbleDictionaryItem
import io.rebble.pebblekit2.common.model.TransmissionResult
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

        /** The Pebble app is not installed, or has not granted this app access. */
        data object NoPebbleApp : Outcome()

        data object NoWatchConnected : Outcome()

        data class Failed(val reason: String) : Outcome()
    }

    suspend fun send(context: Context, pass: EncodedPass): Outcome = send(
        context,
        mapOf(
            KEY_VERSION to PebbleDictionaryItem.UInt8(PROTOCOL_VERSION),
            KEY_MODULES to PebbleDictionaryItem.UInt8(pass.modules),
            KEY_MATRIX to PebbleDictionaryItem.Bytes(pass.packed),
            KEY_LABEL to PebbleDictionaryItem.Text(pass.label.take(MAX_LABEL_LENGTH)),
        ),
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
    )

    private suspend fun send(
        context: Context,
        data: Map<UInt, PebbleDictionaryItem>,
    ): Outcome {
        val sender = DefaultPebbleSender(context.applicationContext)
        try {
            val results = sender.sendDataToPebble(WATCHAPP_UUID, data)
                ?: return Outcome.NoPebbleApp

            if (results.isEmpty()) {
                return Outcome.NoWatchConnected
            }
            // One success is enough: the pass is on a watch the user is wearing.
            if (results.values.any { it is TransmissionResult.Success }) {
                return Outcome.Sent
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
}
