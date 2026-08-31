package com.pebblewatchapps.boardingpass

import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.MultiFormatWriter
import com.google.zxing.WriterException
import com.google.zxing.common.BitMatrix
import com.google.zxing.datamatrix.encoder.SymbolShapeHint
import com.google.zxing.qrcode.decoder.ErrorCorrectionLevel

/**
 * Re-encodes a boarding pass payload as a symbol the watch can actually draw,
 * and packs the modules into the bit layout the watchapp unpacks.
 *
 * The symbology the airline issued is kept whenever the watch can show it, so
 * the gate reader sees exactly the symbol it was given. Only when that is
 * impossible - PDF417 is 205 modules wide and cannot be drawn on a 200 px screen
 * at any usable scale - does this fall back to Aztec, which is both the most
 * compact of the square symbologies and the only one needing no quiet zone.
 * A fallback is reported rather than done quietly: the phone app asks before
 * sending a symbol the airline did not issue.
 */
object SymbolEncoder {

    /**
     * Set by persistent storage on the watch, not by the screen: one persist key
     * holds 256 bytes and ceil(45*45/8) = 254. Mirrors code_matrix.h.
     */
    const val MAX_MODULES = 45

    /** Symbologies that are square, and so drawable as a module grid. */
    private val SQUARE_FORMATS = setOf(
        BarcodeFormat.AZTEC,
        BarcodeFormat.QR_CODE,
        BarcodeFormat.DATA_MATRIX,
    )

    private val FALLBACK_FORMAT = BarcodeFormat.AZTEC

    /**
     * Quiet zone each symbology needs around it, in modules. Aztec needs none
     * by design; Data Matrix needs one; QR asks for four.
     */
    private val QUIET_ZONE_MODULES = mapOf(
        BarcodeFormat.AZTEC to 0,
        BarcodeFormat.DATA_MATRIX to 1,
        BarcodeFormat.QR_CODE to 4,
    )

    /** Nothing fits, even as Aztec at the lowest sane error correction. */
    class TooLargeException(val modules: Int) : Exception(
        "the barcode needs $modules modules, more than the $MAX_MODULES the watch can hold"
    )

    fun encode(payload: String, sourceFormat: BarcodeFormat, label: String): EncodedPass {
        if (sourceFormat in SQUARE_FORMATS) {
            fit(payload, sourceFormat)?.let { fitted ->
                return EncodedPass(
                    modules = fitted.modules,
                    packed = fitted.packed,
                    label = label,
                    format = sourceFormat,
                    sourceFormat = sourceFormat,
                )
            }
        }

        val fallback = fit(payload, FALLBACK_FORMAT)
            ?: throw TooLargeException(smallestAttempt(payload, FALLBACK_FORMAT))
        return EncodedPass(
            modules = fallback.modules,
            packed = fallback.packed,
            label = label,
            format = FALLBACK_FORMAT,
            sourceFormat = sourceFormat,
        )
    }

    private class Fitted(val modules: Int, val packed: ByteArray)

    /** The first encoding of [payload] in [format] that the watch can show. */
    private fun fit(payload: String, format: BarcodeFormat): Fitted? {
        for (hints in hintLadder(format)) {
            val matrix = write(payload, format, hints) ?: continue
            // A rectangular symbol is not a module grid the watch can draw.
            if (matrix.width != matrix.height) continue
            if (fitsOnWatch(matrix.width, QUIET_ZONE_MODULES[format] ?: 0)) {
                return Fitted(matrix.width, pack(matrix))
            }
        }
        return null
    }

    private fun smallestAttempt(payload: String, format: BarcodeFormat): Int =
        hintLadder(format)
            .mapNotNull { write(payload, format, it)?.width }
            .minOrNull() ?: Int.MAX_VALUE

    private fun write(
        payload: String,
        format: BarcodeFormat,
        hints: Map<EncodeHintType, Any>,
    ): BitMatrix? = try {
        // Width and height of 0 tell the writers not to scale: the result is one
        // pixel per module, which is exactly the matrix we want.
        MultiFormatWriter().encode(payload, format, 0, 0, hints)
    } catch (_: WriterException) {
        null
    } catch (_: IllegalArgumentException) {
        null
    }

    /**
     * Settings to try for each symbology, roomiest error correction first.
     *
     * ZXing defaults Aztec to 33% error correction, which can push the symbol a
     * size larger than the one the airline app itself shows; Aztec's own floor
     * is 23%, so step down towards it rather than give up. QR steps from medium
     * to low the same way. Data Matrix has no choice to make, but does need to
     * be told to stay square - left alone it will happily return a rectangle.
     */
    private fun hintLadder(format: BarcodeFormat): List<Map<EncodeHintType, Any>> = when (format) {
        BarcodeFormat.AZTEC -> listOf(33, 28, 25, 23).map {
            mapOf(EncodeHintType.ERROR_CORRECTION to it, EncodeHintType.MARGIN to 0)
        }

        BarcodeFormat.QR_CODE -> listOf(ErrorCorrectionLevel.M, ErrorCorrectionLevel.L).map {
            mapOf(EncodeHintType.ERROR_CORRECTION to it, EncodeHintType.MARGIN to 0)
        }

        BarcodeFormat.DATA_MATRIX -> listOf(
            mapOf(
                EncodeHintType.DATA_MATRIX_SHAPE to SymbolShapeHint.FORCE_SQUARE,
                EncodeHintType.MARGIN to 0,
            )
        )

        else -> emptyList()
    }

    /**
     * Whether the watch can draw [modules] modules and still leave
     * [quietZoneModules] of white around the symbol.
     *
     * This mirrors code_pixels_per_module() in the watchapp's code_matrix.h. The
     * watch centres the symbol and knows nothing about symbologies, so deciding
     * here is what keeps a QR code from being sent without the quiet zone it
     * needs to be read.
     */
    fun fitsOnWatch(modules: Int, quietZoneModules: Int): Boolean {
        if (modules > MAX_MODULES) {
            return false
        }
        val pixelsPerModule = minOf(MAX_PX_PER_MODULE, (SCREEN_WIDTH - SIDE_MARGIN) / modules)
        if (pixelsPerModule < MIN_PX_PER_MODULE) {
            return false
        }
        val marginPixels = (SCREEN_WIDTH - modules * pixelsPerModule) / 2
        return marginPixels >= quietZoneModules * pixelsPerModule
    }

    /** Row by row, MSB first - the layout code_module_is_set() reads back. */
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

    // Pebble Time 2, mirroring code_matrix.h.
    private const val SCREEN_WIDTH = 200
    private const val SIDE_MARGIN = 14
    private const val MAX_PX_PER_MODULE = 5

    /** Below this a scanner cannot read the symbol off a 202 ppi reflective screen. */
    private const val MIN_PX_PER_MODULE = 4
}
