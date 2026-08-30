package com.pebblewatchapps.boardingpass

/**
 * Just enough of IATA Resolution 792 (BCBP) to name a flight.
 *
 * The mandatory part of an M1 string is a fixed 60-character block, so the few
 * fields worth showing on the watch can be sliced straight out of it. Nothing
 * here touches the passenger name or the booking reference: those stay on the
 * phone.
 */
object Bcbp {

    private const val MANDATORY_LENGTH = 60

    private val CARRIER = 36 until 39
    private val FLIGHT_NUMBER = 39 until 44
    private val SEAT = 48 until 52

    /**
     * A short flight label such as "SK4174 12A", or null when the payload is
     * not an M1 boarding pass at all (the app still works - the watch just
     * shows the code without a caption).
     */
    fun label(payload: String): String? {
        if (payload.length < MANDATORY_LENGTH || payload[0] != 'M') {
            return null
        }

        val carrier = payload.substring(CARRIER).trim()
        val flight = payload.substring(FLIGHT_NUMBER).trim().trimStart('0')
        val seat = payload.substring(SEAT).trim().trimStart('0')

        if (carrier.isEmpty() || flight.isEmpty()) {
            return null
        }
        return if (seat.isEmpty()) "$carrier$flight" else "$carrier$flight $seat"
    }
}
