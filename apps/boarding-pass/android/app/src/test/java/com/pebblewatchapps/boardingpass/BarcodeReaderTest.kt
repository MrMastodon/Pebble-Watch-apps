package com.pebblewatchapps.boardingpass

import android.net.Uri
import androidx.test.core.app.ApplicationProvider
import com.google.zxing.BarcodeFormat
import com.google.zxing.aztec.AztecWriter
import java.awt.Color
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
    fun `reads an aztec code out of a tightly cropped image`() {
        val file = screenshotContaining(
            SYNTHETIC_BCBP,
            name = "tight.png",
            canvasWidth = 420,
            canvasHeight = 420,
            top = 30,
            scale = 10,
        )

        val payload = BarcodeReader.read(
            ApplicationProvider.getApplicationContext(),
            Uri.fromFile(file),
        )

        assertEquals(SYNTHETIC_BCBP, payload)
    }

    /**
     * The shape that actually broke in the field: a full-height phone
     * screenshot with the code up near the top and a large empty area below it.
     * ZXing's Aztec detector searches outwards from the middle of the image,
     * which here is blank, so decoding the image as a whole finds nothing.
     */
    @Test
    fun `reads a code sitting near the top of a full phone screenshot`() {
        val file = screenshotContaining(
            SYNTHETIC_BCBP,
            name = "phone.png",
            canvasWidth = 1080,
            canvasHeight = 2340,
            top = 300,
            scale = 10,
        )

        val payload = BarcodeReader.read(
            ApplicationProvider.getApplicationContext(),
            Uri.fromFile(file),
        )

        assertEquals(SYNTHETIC_BCBP, payload)
    }

    @Test
    fun `reports an image with no barcode in it`() {
        val blank = temporaryFolder.newFile("blank.png")
        val white = BufferedImage(1080, 2340, BufferedImage.TYPE_INT_RGB)
        white.createGraphics().apply {
            paint = Color.WHITE
            fillRect(0, 0, white.width, white.height)
            dispose()
        }
        ImageIO.write(white, "png", blank)

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
     * An Aztec symbol drawn into a canvas of the given size, at the given
     * offset, with everything else left white - roughly how the code sits in a
     * screenshot surrounded by app chrome.
     */
    private fun screenshotContaining(
        payload: String,
        name: String,
        canvasWidth: Int,
        canvasHeight: Int,
        top: Int,
        scale: Int,
    ): File {
        val matrix = AztecWriter().encode(payload, BarcodeFormat.AZTEC, 0, 0)
        val left = (canvasWidth - matrix.width * scale) / 2

        val image = BufferedImage(canvasWidth, canvasHeight, BufferedImage.TYPE_INT_RGB)
        val graphics = image.createGraphics()
        graphics.paint = Color.WHITE
        graphics.fillRect(0, 0, canvasWidth, canvasHeight)
        graphics.paint = Color.BLACK
        for (row in 0 until matrix.height) {
            for (column in 0 until matrix.width) {
                if (matrix.get(column, row)) {
                    graphics.fillRect(left + column * scale, top + row * scale, scale, scale)
                }
            }
        }
        graphics.dispose()

        val file = temporaryFolder.newFile(name)
        ImageIO.write(image, "png", file)
        return file
    }

    private companion object {
        // Synthetic, invented values. Real BCBP data must never be committed.
        const val SYNTHETIC_BCBP =
            "M1TESTER/SYNTHETIC    EZZ9XY9 OSLCPHSK 4174 250Y012A0034 147>50B0WW5180BSK 2A117000000000000SK SK 0000000000000000 20KNSYNTHETIC   "
    }
}
