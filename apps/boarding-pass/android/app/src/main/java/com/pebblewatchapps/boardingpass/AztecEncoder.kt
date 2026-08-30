package com.pebblewatchapps.boardingpass

import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.aztec.AztecWriter
import com.google.zxing.common.BitMatrix

/**
 * Re-encodes a barcode payload as an Aztec symbol the watch can draw legibly,
 * and packs the modules into the bit layout the watchapp unpacks.
 *
 * All Aztec work happens here rather than on the watch: the watch only ever
 * receives finished bits.
 */
object AztecEncoder {

    /**
     * The watchapp refuses anything larger (see aztec_matrix.h). At 41 modules
     * the watch draws 4 px per module, about as fine as a scanner manages off a
     * 202 ppi reflective display.
     */
    const val MAX_MODULES = 41

    /**
     * ZXing defaults to 33% error correction, which can push the symbol a size
     * larger than the one the airline app itself shows. Aztec's own floor is
     * 23%, so step down towards it instead of handing the watch a symbol it
     * cannot draw at a readable scale.
     */
    private val ERROR_CORRECTION_PERCENTS = intArrayOf(33, 28, 25, 23)

    /** The payload does not fit on the watch even at the lowest sane ECC. */
    class TooLargeException(val modules: Int) : Exception(
        "the barcode needs $modules modules, more than the $MAX_MODULES the watch can draw"
    )

    fun encode(payload: String, label: String): EncodedPass {
        var modules = 0
        for (percent in ERROR_CORRECTION_PERCENTS) {
            // Width and height of 0 tell AztecWriter not to scale: the result
            // is one pixel per module, which is exactly the matrix we want.
            val matrix = AztecWriter().encode(
                payload,
                BarcodeFormat.AZTEC,
                0,
                0,
                mapOf(EncodeHintType.ERROR_CORRECTION to percent),
            )
            modules = matrix.width
            if (modules <= MAX_MODULES) {
                return EncodedPass(modules, pack(matrix), label)
            }
        }
        throw TooLargeException(modules)
    }

    /** Row by row, MSB first - the layout aztec_module_is_set() reads back. */
    fun pack(matrix: BitMatrix): ByteArray {
        val modules = matrix.width
        val packed = ByteArray((modules * modules + 7) / 8)
        var index = 0
        for (row in 0 until modules) {
            for (column in 0 until modules) {
                if (matrix.get(column, row)) {
                    packed[index / 8] = (packed[index / 8].toInt() or (0x80 ushr (index % 8))).toByte()
                }
                index++
            }
        }
        return packed
    }
}
