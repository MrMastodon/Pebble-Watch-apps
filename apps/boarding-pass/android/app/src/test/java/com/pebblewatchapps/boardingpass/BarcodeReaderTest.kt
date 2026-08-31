package com.pebblewatchapps.boardingpass

import android.net.Uri
import androidx.test.core.app.ApplicationProvider
import com.google.zxing.BarcodeFormat
import com.google.zxing.aztec.AztecWriter
import java.awt.image.BufferedImage
import java.io.File
import javax.imageio.ImageIO
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.GraphicsMode

/**
 * Reading an image is the one part of this app that cannot be tested without a
 * real Android graphics stack, so it runs under Robolectric in native graphics
 * mode: BitmapFactory really decodes, and getPixels really reads back.
 *
 * The first version of loadBitmap() failed every single image because the
 * elvis operator bound to the result of `use { }` rather than to
 * openInputStream(), and decodeStream() returns null by contract when
 * inJustDecodeBounds is set. That shipped because nothing exercised this path.
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class BarcodeReaderTest {

    @get:Rule
    val temporaryFolder = TemporaryFolder()

    @Test
    fun `reads an aztec code out of a screenshot`() {
        val uri = Uri.fromFile(screenshotContaining(SYNTHETIC_BCBP))

        val payload = BarcodeReader.read(ApplicationProvider.getApplicationContext(), uri)

        assertEquals(SYNTHETIC_BCBP, payload)
    }

    @Test
    fun `reports an image with no barcode in it`() {
        val blank = temporaryFolder.newFile("blank.png")
        ImageIO.write(BufferedImage(400, 800, BufferedImage.TYPE_INT_RGB), "png", blank)

        val error = runCatching {
            BarcodeReader.read(ApplicationProvider.getApplicationContext(), Uri.fromFile(blank))
        }.exceptionOrNull()

        assertTrue("got $error", error is BarcodeReader.NotFoundInImageException)
    }

    @Test
    fun `reports a file that is not an image`() {
        val notAnImage = temporaryFolder.newFile("notes.txt")
        notAnImage.writeText("this is not a screenshot")

        val error = runCatching {
            BarcodeReader.read(ApplicationProvider.getApplicationContext(), Uri.fromFile(notAnImage))
        }.exceptionOrNull()

        assertTrue("got $error", error is BarcodeReader.UnreadableImageException)
    }

    /**
     * An Aztec symbol drawn small inside a much larger white canvas, roughly how
     * it sits in a phone screenshot surrounded by app chrome.
     */
    private fun screenshotContaining(payload: String): File {
        val matrix = AztecWriter().encode(payload, BarcodeFormat.AZTEC, 0, 0)
        val scale = 8
        val margin = 60

        val image = BufferedImage(
            matrix.width * scale + margin * 2,
            matrix.height * scale + margin * 2 + 200,
            BufferedImage.TYPE_INT_RGB,
        )
        val graphics = image.createGraphics()
        graphics.paint = java.awt.Color.WHITE
        graphics.fillRect(0, 0, image.width, image.height)
        graphics.paint = java.awt.Color.BLACK
        for (row in 0 until matrix.height) {
            for (column in 0 until matrix.width) {
                if (matrix.get(column, row)) {
                    graphics.fillRect(margin + column * scale, margin + row * scale, scale, scale)
                }
            }
        }
        graphics.dispose()

        val file = temporaryFolder.newFile("screenshot.png")
        ImageIO.write(image, "png", file)
        return file
    }

    private companion object {
        // Synthetic, invented values. Real BCBP data must never be committed.
        const val SYNTHETIC_BCBP =
            "M1TESTER/SYNTHETIC    EZZ9XY9 OSLCPHSK 4174 250Y012A0034 147>50B0WW5180BSK 2A117000000000000SK SK 0000000000000000 20KNSYNTHETIC   "
    }
}
