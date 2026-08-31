package com.pebblewatchapps.boardingpass

import com.google.zxing.BarcodeFormat
import com.google.zxing.aztec.AztecWriter
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The packing here has to agree, bit for bit, with code_module_is_set() in the
 * watchapp's code_matrix.h. scripts/roundtrip.py checks the other end of that
 * agreement by decoding what the watch code actually draws.
 */
class SymbolEncoderTest {

    @Test
    fun `packs row by row with the most significant bit first`() {
        val matrix = AztecWriter().encode("X", BarcodeFormat.AZTEC, 0, 0)
        val packed = SymbolEncoder.pack(matrix)

        assertEquals((matrix.width * matrix.width + 7) / 8, packed.size)
        for (index in 0 until matrix.width * matrix.width) {
            val row = index / matrix.width
            val column = index % matrix.width
            val bit = (packed[index / 8].toInt() shr (7 - index % 8)) and 1
            assertEquals("module ($column, $row)", if (matrix.get(column, row)) 1 else 0, bit)
        }
    }

    @Test
    fun `keeps the symbology the airline issued when the watch can draw it`() {
        for (format in listOf(
            BarcodeFormat.AZTEC, BarcodeFormat.QR_CODE, BarcodeFormat.DATA_MATRIX,
        )) {
            val pass = SymbolEncoder.encode(SYNTHETIC_BCBP, format, "SK4174 12A")

            assertEquals("$format should be kept", format, pass.format)
            assertFalse(pass.isSubstituted)
            assertTrue(
                "$format needed ${pass.modules} modules",
                pass.modules <= SymbolEncoder.MAX_MODULES,
            )
            assertEquals((pass.modules * pass.modules + 7) / 8, pass.packed.size)
            // One AppMessage, one 256 byte persist key.
            assertTrue("$format packed to ${pass.packed.size} bytes", pass.packed.size <= 256)
        }
    }

    @Test
    fun `falls back to aztec for pdf417, which is far too wide to draw`() {
        val pass = SymbolEncoder.encode(SYNTHETIC_BCBP, BarcodeFormat.PDF_417, "SK4174 12A")

        assertEquals(BarcodeFormat.AZTEC, pass.format)
        assertEquals(BarcodeFormat.PDF_417, pass.sourceFormat)
        assertTrue(pass.isSubstituted)
        assertTrue(pass.modules <= SymbolEncoder.MAX_MODULES)
    }

    @Test
    fun `data matrix is forced square, since the watch draws a square grid`() {
        // Left to itself ZXing returns a rectangular Data Matrix for this
        // payload, which the watch cannot draw at all.
        val pass = SymbolEncoder.encode(SYNTHETIC_BCBP, BarcodeFormat.DATA_MATRIX, "test")

        assertEquals(BarcodeFormat.DATA_MATRIX, pass.format)
        assertEquals((pass.modules * pass.modules + 7) / 8, pass.packed.size)
    }

    @Test
    fun `refuses a symbol that would not leave room for its quiet zone`() {
        // Mirrors code_pixels_per_module(): at 41 modules the watch draws 4 px
        // each, leaving 18 px - four and a half modules - on either side.
        assertTrue("QR at 41 has room for four modules", SymbolEncoder.fitsOnWatch(41, 4))
        assertFalse("QR at 45 does not", SymbolEncoder.fitsOnWatch(45, 4))
        // Aztec asks for no quiet zone, so it only has to fit.
        assertTrue(SymbolEncoder.fitsOnWatch(45, 0))
        assertFalse(SymbolEncoder.fitsOnWatch(46, 0))
    }

    @Test
    fun `an oversized payload is rejected rather than truncated`() {
        val error = runCatching {
            SymbolEncoder.encode("X".repeat(2000), BarcodeFormat.AZTEC, "test")
        }.exceptionOrNull()

        assertTrue("got $error", error is SymbolEncoder.TooLargeException)
    }

    private companion object {
        // Synthetic, invented values. Real BCBP data must never be committed.
        const val SYNTHETIC_BCBP =
            "M1TESTER/SYNTHETIC    EZZ9XY9 OSLCPHSK 4174 250Y012A0034 147>50B0WW5180BSK 2A117000000000000SK SK 0000000000000000 20KNSYNTHETIC   "
    }
}
