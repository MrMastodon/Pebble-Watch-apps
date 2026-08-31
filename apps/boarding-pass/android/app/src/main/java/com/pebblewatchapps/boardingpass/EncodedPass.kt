package com.pebblewatchapps.boardingpass

import com.google.zxing.BarcodeFormat

/**
 * A boarding pass barcode in the only form the watch understands: a square
 * module matrix, packed row by row with the most significant bit first.
 *
 * [label] is derived from the BCBP flight fields. It deliberately never carries
 * the passenger name or the booking reference - the watch has no use for them,
 * and anything sent there is one more place they can leak from.
 */
class EncodedPass(
    val modules: Int,
    val packed: ByteArray,
    val label: String,
    /** The symbology the watch will draw. */
    val format: BarcodeFormat,
    /** The symbology the airline issued. */
    val sourceFormat: BarcodeFormat,
) {
    /**
     * True when the watch will show a different symbology than the airline
     * issued - which only happens when the original cannot be drawn at all.
     * The gate reader should still accept it, since IATA allows all of these,
     * but that is the user's call to make rather than ours.
     */
    val isSubstituted: Boolean get() = format != sourceFormat
}
