package com.pebblewatchapps.boardingpass

import com.google.zxing.BarcodeFormat
import com.google.zxing.aztec.AztecWriter
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The packing here has to agree, bit for bit, with aztec_module_is_set() in the
 * watchapp's aztec_matrix.h. scripts/roundtrip.py checks the other end of that
 * agreement by decoding what the watch code actually draws.
 */
class AztecEncoderTest {

    @Test
    fun `packs row by row with the most significant bit first`() {
        // A 15x15 symbol is the smallest Aztec size, so the first byte covers
        // the first eight modules of the top row and nothing else.
        val matrix = AztecWriter().encode("X", BarcodeFormat.AZTEC, 0, 0)
        val packed = AztecEncoder.pack(matrix)

        assertEquals((matrix.width * matrix.width + 7) / 8, packed.size)
        for (index in 0 until matrix.width * matrix.width) {
            val row = index / matrix.width
            val column = index % matrix.width
            val bit = (packed[index / 8].toInt() shr (7 - index % 8)) and 1
            assertEquals(
                "module ($column, $row)",
                if (matrix.get(column, row)) 1 else 0,
                bit,
            )
        }
    }

    @Test
    fun `a boarding pass sized payload fits the watch`() {
        val pass = AztecEncoder.encode(SYNTHETIC_BCBP, "SK4174 12A")

        assertTrue(
            "needed ${pass.modules} modules",
            pass.modules <= AztecEncoder.MAX_MODULES,
        )
        assertEquals((pass.modules * pass.modules + 7) / 8, pass.packed.size)
        // The whole design rests on this fitting in one AppMessage and one
        // 256-byte persist key.
        assertTrue("packed to ${pass.packed.size} bytes", pass.packed.size <= 256)
    }

    @Test
    fun `an oversized payload is rejected rather than truncated`() {
        val tooMuch = "X".repeat(2000)

        val error = runCatching { AztecEncoder.encode(tooMuch, "test") }.exceptionOrNull()

        assertTrue("got $error", error is AztecEncoder.TooLargeException)
    }

    private companion object {
        // Synthetic, invented values. Real BCBP data must never be committed.
        const val SYNTHETIC_BCBP =
            "M1TESTER/SYNTHETIC    EZZ9XY9 OSLCPHSK 4174 250Y012A0034 147>50B0WW5180BSK 2A117000000000000SK SK 0000000000000000 20KNSYNTHETIC   "
    }
}
