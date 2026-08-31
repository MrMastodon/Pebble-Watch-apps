package com.pebblewatchapps.boardingpass

import android.net.Uri
import androidx.test.core.app.ApplicationProvider
import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.MultiFormatWriter
import com.google.zxing.datamatrix.encoder.SymbolShapeHint
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
 * Two bugs shipped from this file being thin. The first rejected every image,
 * because an elvis operator bound to the result of `use { }` while
 * decodeStream() returns null by contract under inJustDecodeBounds. The second
 * found nothing in a real screenshot, because ZXing's Aztec detector searches
 * outwards from the centre of the image and a boarding pass sits near the top.
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class BarcodeReaderTest {

    @get:Rule
    val temporaryFolder = TemporaryFolder()

    /**
     * The whole point of the search: every symbology IATA allows on a boarding
     * pass, anywhere on the screen, at any size a phone would show it at.
     */
    @Test
    fun `reads every symbology, wherever it sits and whatever size it is`() {
        val positions = listOf(
            "top-left" to (0.0 to 0.0),
            "top-centre" to (0.5 to 0.1),
            "top-right" to (1.0 to 0.0),
            "centre" to (0.5 to 0.5),
            "bottom-left" to (0.0 to 1.0),
            "bottom-right" to (1.0 to 1.0),
        )
        val scales = listOf(4, 10)
        val failures = mutableListOf<String>()

        for (format in FORMATS) {
            for ((positionName, position) in positions) {
                for (scale in scales) {
                    val case = "$format at $positionName, scale $scale"
                    val file = screenshotContaining(
                        format = format,
                        name = "${format.name}-$positionName-$scale.png",
                        fractionX = position.first,
                        fractionY = position.second,
                        scale = scale,
                    )
                    val scanned = runCatching {
                        BarcodeReader.read(
                            ApplicationProvider.getApplicationContext(),
                            Uri.fromFile(file),
                        )
                    }.getOrNull()

                    when {
                        scanned == null -> failures += "$case: not found"
                        scanned.text != SYNTHETIC_BCBP -> failures += "$case: wrong payload"
                        scanned.format != format -> failures += "$case: read as ${scanned.format}"
                    }
                }
            }
        }

        assertEquals("failures:\n" + failures.joinToString("\n"), emptyList<String>(), failures)
    }

    @Test
    fun `reports an image with no barcode in it`() {
        val blank = temporaryFolder.newFile("blank.png")
        val white = BufferedImage(CANVAS_WIDTH, CANVAS_HEIGHT, BufferedImage.TYPE_INT_RGB)
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

    /** A symbol drawn into a full-size phone screenshot at the given position. */
    private fun screenshotContaining(
        format: BarcodeFormat,
        name: String,
        fractionX: Double,
        fractionY: Double,
        scale: Int,
    ): File {
        val hints = buildMap<EncodeHintType, Any> {
            put(EncodeHintType.MARGIN, 0)
            if (format == BarcodeFormat.DATA_MATRIX) {
                put(EncodeHintType.DATA_MATRIX_SHAPE, SymbolShapeHint.FORCE_SQUARE)
            }
        }
        val matrix = MultiFormatWriter().encode(SYNTHETIC_BCBP, format, 0, 0, hints)

        // PDF417 is far wider than it is tall; shrink it to fit across.
        var moduleWidth = scale.toDouble()
        var moduleHeight = scale.toDouble()
        if (matrix.width * moduleWidth > CANVAS_WIDTH - 2 * INSET) {
            moduleWidth = (CANVAS_WIDTH - 2.0 * INSET) / matrix.width
            moduleHeight = moduleWidth
        }
        val symbolWidth = (matrix.width * moduleWidth).toInt()
        val symbolHeight = (matrix.height * moduleHeight).toInt()

        val image = BufferedImage(CANVAS_WIDTH, CANVAS_HEIGHT, BufferedImage.TYPE_INT_RGB)
        val graphics = image.createGraphics()
        graphics.paint = Color.WHITE
        graphics.fillRect(0, 0, CANVAS_WIDTH, CANVAS_HEIGHT)
        graphics.paint = Color.BLACK

        // Inset so the symbol is never clipped by the canvas edge: a clipped
        // finder pattern is genuinely unreadable, by any decoder.
        val left = INSET + ((CANVAS_WIDTH - symbolWidth - 2 * INSET) * fractionX).toInt()
        val top = INSET + ((CANVAS_HEIGHT - symbolHeight - 2 * INSET) * fractionY).toInt()
        for (row in 0 until matrix.height) {
            for (column in 0 until matrix.width) {
                if (matrix.get(column, row)) {
                    graphics.fillRect(
                        left + (column * moduleWidth).toInt(),
                        top + (row * moduleHeight).toInt(),
                        Math.ceil(moduleWidth).toInt().coerceAtLeast(1),
                        Math.ceil(moduleHeight).toInt().coerceAtLeast(1),
                    )
                }
            }
        }
        graphics.dispose()

        val file = temporaryFolder.newFile(name)
        ImageIO.write(image, "png", file)
        return file
    }

    private companion object {
        val FORMATS = listOf(
            BarcodeFormat.AZTEC,
            BarcodeFormat.QR_CODE,
            BarcodeFormat.DATA_MATRIX,
            BarcodeFormat.PDF_417,
        )

        const val CANVAS_WIDTH = 1080
        const val CANVAS_HEIGHT = 2340
        const val INSET = 8

        // Synthetic, invented values. Real BCBP data must never be committed.
        const val SYNTHETIC_BCBP =
            "M1TESTER/SYNTHETIC    EZZ9XY9 OSLCPHSK 4174 250Y012A0034 147>50B0WW5180BSK 2A117000000000000SK SK 0000000000000000 20KNSYNTHETIC   "
    }
}
