package com.pebblewatchapps.boardingpass

import com.google.zxing.RGBLuminanceSource
import java.util.Random
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The candidate regions the reader searches. Pure ZXing, no Android needed.
 *
 * A busy image - a photo of a screen, anything with detail everywhere - comes
 * back as one blob spanning the whole thing. Centring that in its own padded
 * canvas once cost 43 MB in a single allocation on a 1080x2340 image, and an
 * OutOfMemoryError is an Error rather than an Exception, so it crashed the app
 * instead of failing the import.
 */
class SearchRegionsTest {

    @Test
    fun `no candidate region is larger than the budget`() {
        val source = noise(1080, 2340)

        val regions = BarcodeReader.searchRegions(source).toList()

        for (region in regions.drop(1)) {
            val pixels = region.width.toLong() * region.height
            assertTrue(
                "candidate ${region.width}x${region.height} is " +
                    "${pixels / 1_000_000} MP, over the " +
                    "${BarcodeReader.MAX_CANDIDATE_PIXELS / 1_000_000} MP budget",
                pixels <= BarcodeReader.MAX_CANDIDATE_PIXELS,
            )
        }
    }

    @Test
    fun `the whole image is always tried first`() {
        val source = noise(400, 800)

        val first = BarcodeReader.searchRegions(source).first()

        assertTrue(first === source)
    }

    /**
     * A barcode sharing a tall card with text and a logo comes back as one tall
     * blob. Capping the canvas used to drop those candidates outright, which
     * lost exactly the boarding passes that need centring most.
     */
    @Test
    fun `a blob too tall for the canvas is scaled in, not dropped`() {
        val source = markAt(1080, 2340, left = 40, top = 200, markWidth = 1000, markHeight = 1800)

        val regions = BarcodeReader.searchRegions(source).toList()

        assertTrue("the oversized blob was dropped", regions.size > 1)
        for (region in regions.drop(1)) {
            assertTrue(
                "candidate ${region.width}x${region.height} is over budget",
                region.width.toLong() * region.height <= BarcodeReader.MAX_CANDIDATE_PIXELS,
            )
        }
    }

    @Test
    fun `a localised mark still gets a candidate of its own`() {
        // What a boarding pass screenshot looks like: one busy patch, the rest
        // blank. This must keep producing a centred candidate.
        val source = markAt(1080, 2340, left = 340, top = 300, markWidth = 400, markHeight = 400)

        val regions = BarcodeReader.searchRegions(source).toList()

        assertTrue("expected a candidate besides the whole image", regions.size > 1)
        for (region in regions.drop(1)) {
            assertTrue(
                region.width.toLong() * region.height <= BarcodeReader.MAX_CANDIDATE_PIXELS,
            )
        }
    }

    private fun noise(width: Int, height: Int): RGBLuminanceSource {
        val random = Random(7)
        val pixels = IntArray(width * height) {
            val value = 90 + random.nextInt(120)
            0xFF000000.toInt() or (value shl 16) or (value shl 8) or value
        }
        return RGBLuminanceSource(width, height, pixels)
    }

    private fun markAt(
        width: Int,
        height: Int,
        left: Int,
        top: Int,
        markWidth: Int,
        markHeight: Int,
    ): RGBLuminanceSource {
        val pixels = IntArray(width * height) { 0xFFFFFFFF.toInt() }
        for (y in top until top + markHeight) {
            for (x in left until left + markWidth) {
                // A checkerboard: high local contrast, like a barcode.
                if ((x / 4 + y / 4) % 2 == 0) {
                    pixels[y * width + x] = 0xFF000000.toInt()
                }
            }
        }
        return RGBLuminanceSource(width, height, pixels)
    }
}
