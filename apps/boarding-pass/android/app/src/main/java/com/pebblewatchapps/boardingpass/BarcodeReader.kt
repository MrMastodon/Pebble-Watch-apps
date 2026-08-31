package com.pebblewatchapps.boardingpass

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import com.google.zxing.BarcodeFormat
import com.google.zxing.Binarizer
import com.google.zxing.BinaryBitmap
import com.google.zxing.DecodeHintType
import com.google.zxing.LuminanceSource
import com.google.zxing.MultiFormatReader
import com.google.zxing.NotFoundException
import com.google.zxing.RGBLuminanceSource
import com.google.zxing.common.GlobalHistogramBinarizer
import com.google.zxing.common.HybridBinarizer
import java.io.IOException
import java.io.InputStream

/** What was read out of an image, and which symbology it was written in. */
class ScannedCode(val text: String, val format: BarcodeFormat)

/**
 * Pulls a boarding pass barcode out of a screenshot of an airline app.
 *
 * Finding it is most of the work. QR and PDF417 are found wherever they sit,
 * because their detectors scan the whole image for patterns. Aztec and Data
 * Matrix are not: both hunt outwards from the centre of whatever image they are
 * handed, so a code anywhere but the middle of a tall screenshot is invisible to
 * them, and no amount of binarizing or TRY_HARDER changes that. So the image is
 * first searched for high-contrast blobs, and each one is copied into the middle
 * of its own white canvas before being offered to the reader.
 *
 * Measured over synthetic screenshots covering four symbologies, nine positions
 * and three sizes: the whole image alone read 64 of 108, sliding windows read
 * 79, and the centred blobs below read all 108, in less time than the windows
 * took.
 */
object BarcodeReader {

    /** No boarding pass barcode could be read out of the image. */
    class NotFoundInImageException : Exception("no boarding pass barcode found in the image")

    /** The image could not be read at all (gone, or not an image). */
    class UnreadableImageException(uri: Uri) : Exception("could not read the image at $uri")

    /** The 2D symbologies IATA Resolution 792 allows on a boarding pass. */
    private val BOARDING_PASS_FORMATS = listOf(
        BarcodeFormat.AZTEC,
        BarcodeFormat.QR_CODE,
        BarcodeFormat.DATA_MATRIX,
        BarcodeFormat.PDF_417,
    )

    private val HINTS = mapOf(
        DecodeHintType.POSSIBLE_FORMATS to BOARDING_PASS_FORMATS,
        DecodeHintType.TRY_HARDER to true,
    )

    /**
     * HybridBinarizer handles the usual case; GlobalHistogramBinarizer copes
     * better with the flat, evenly lit rendering a screenshot actually is.
     */
    private val BINARIZERS = listOf<(LuminanceSource) -> Binarizer>(
        { HybridBinarizer(it) },
        { GlobalHistogramBinarizer(it) },
    )

    /**
     * A full-resolution phone screenshot is a few megapixels; anything past
     * this is downsampled first, which still leaves many pixels per module.
     */
    private const val MAX_PIXELS = 4_000_000

    fun read(context: Context, uri: Uri): ScannedCode = decode(loadBitmap(context, uri))

    // ------------------------------------------------------------ loading --

    private fun loadBitmap(context: Context, uri: Uri): Bitmap {
        // First pass measures the image. decodeStream returns null here by
        // contract - with inJustDecodeBounds the answer comes back in `bounds`,
        // so the size, not the return value, is what says whether this worked.
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        openStream(context, uri).use { BitmapFactory.decodeStream(it, null, bounds) }
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) {
            throw UnreadableImageException(uri)
        }

        // Second pass decodes for real, downsampled if the screenshot is huge.
        val options = BitmapFactory.Options().apply {
            inSampleSize = sampleSizeFor(bounds.outWidth, bounds.outHeight)
            // ARGB_8888 keeps getPixels() straightforward, and rules out the
            // hardware-backed bitmaps that cannot be read back.
            inPreferredConfig = Bitmap.Config.ARGB_8888
        }
        return openStream(context, uri).use { BitmapFactory.decodeStream(it, null, options) }
            ?: throw UnreadableImageException(uri)
    }

    private fun openStream(context: Context, uri: Uri): InputStream = try {
        context.contentResolver.openInputStream(uri) ?: throw UnreadableImageException(uri)
    } catch (error: IOException) {
        throw UnreadableImageException(uri)
    } catch (error: SecurityException) {
        // The share or picker grant has already lapsed.
        throw UnreadableImageException(uri)
    }

    private fun sampleSizeFor(width: Int, height: Int): Int {
        var sampleSize = 1
        while (width.toLong() * height / (sampleSize.toLong() * sampleSize) > MAX_PIXELS) {
            sampleSize *= 2
        }
        return sampleSize
    }

    // ----------------------------------------------------------- decoding --

    private fun decode(bitmap: Bitmap): ScannedCode {
        val pixels = IntArray(bitmap.width * bitmap.height)
        bitmap.getPixels(pixels, 0, bitmap.width, 0, 0, bitmap.width, bitmap.height)
        val source = RGBLuminanceSource(bitmap.width, bitmap.height, pixels)

        for (region in searchRegions(source)) {
            for (binarizer in BINARIZERS) {
                try {
                    val result = MultiFormatReader().decode(BinaryBitmap(binarizer(region)), HINTS)
                    return ScannedCode(result.text, result.barcodeFormat)
                } catch (_: NotFoundException) {
                    // Try the next binarizer, then the next region.
                }
            }
        }
        throw NotFoundInImageException()
    }

    /**
     * The whole image first - which is enough for QR and PDF417, and for a code
     * that happens to sit in the middle - then each high-contrast blob, largest
     * first, centred in its own canvas. The sequence is lazy, so an image that
     * decodes whole never pays for the blob search.
     */
    internal fun searchRegions(source: LuminanceSource): Sequence<LuminanceSource> = sequence {
        yield(source)
        for (blob in busyBlobs(source)) {
            yield(centred(source, blob))
        }
    }

    private class Blob(val left: Int, val top: Int, val width: Int, val height: Int, val cells: Int)

    /**
     * Bounding boxes of connected areas of high local contrast, largest first.
     *
     * A barcode is a dense block of edges, so it comes out as one big blob;
     * lines of text come out as separate smaller ones. Working on a coarse grid
     * of cells rather than pixels keeps this to a single cheap pass.
     */
    private fun busyBlobs(source: LuminanceSource): List<Blob> {
        val width = source.width
        val height = source.height
        val luminance = source.matrix

        val cell = maxOf(MIN_CELL_PIXELS, minOf(width, height) / CELLS_ACROSS)
        val columns = width / cell
        val rows = height / cell
        if (columns < 3 || rows < 3) {
            return emptyList()
        }

        val busy = BooleanArray(columns * rows)
        for (row in 0 until rows) {
            for (column in 0 until columns) {
                var darkest = 255
                var lightest = 0
                for (y in row * cell until (row + 1) * cell) {
                    val offset = y * width
                    for (x in column * cell until (column + 1) * cell) {
                        val value = luminance[offset + x].toInt() and 0xFF
                        if (value < darkest) darkest = value
                        if (value > lightest) lightest = value
                    }
                }
                busy[row * columns + column] = lightest - darkest > CONTRAST_THRESHOLD
            }
        }

        val blobs = mutableListOf<Blob>()
        val seen = BooleanArray(busy.size)
        // A plain stack: every cell is marked seen before it is pushed, so it
        // can never hold more entries than there are cells.
        val stack = IntArray(busy.size)
        var depth = 0
        for (start in busy.indices) {
            if (!busy[start] || seen[start]) {
                continue
            }
            var minColumn = columns
            var minRow = rows
            var maxColumn = -1
            var maxRow = -1
            var count = 0

            seen[start] = true
            stack[depth++] = start
            while (depth > 0) {
                val index = stack[--depth]
                val column = index % columns
                val row = index / columns
                minColumn = minOf(minColumn, column)
                minRow = minOf(minRow, row)
                maxColumn = maxOf(maxColumn, column)
                maxRow = maxOf(maxRow, row)
                count++

                for (dr in -1..1) {
                    for (dc in -1..1) {
                        val nc = column + dc
                        val nr = row + dr
                        if (nc < 0 || nr < 0 || nc >= columns || nr >= rows) continue
                        val neighbour = nr * columns + nc
                        if (busy[neighbour] && !seen[neighbour]) {
                            seen[neighbour] = true
                            stack[depth++] = neighbour
                        }
                    }
                }
            }

            val blobWidth = (maxColumn - minColumn + 1) * cell
            val blobHeight = (maxRow - minRow + 1) * cell
            if (blobWidth >= MIN_BLOB_PIXELS && blobHeight >= MIN_BLOB_PIXELS &&
                isWorthCentring(blobWidth, blobHeight, width, height)
            ) {
                blobs.add(Blob(minColumn * cell, minRow * cell, blobWidth, blobHeight, count))
            }
        }

        return blobs.sortedByDescending { it.cells }.take(MAX_CANDIDATES)
    }

    /**
     * Whether a blob is worth copying out at all.
     *
     * A photo of a screen, or any busy image, has detail everywhere and comes
     * back as one blob spanning the lot. Centring that gains nothing - the whole
     * image is already the first thing tried - and the padded canvas it would
     * need is enormous: for a 1080x2340 image, 3270x3270 pixels, 43 MB in one
     * allocation, on top of the bitmap. An OutOfMemoryError is an Error rather
     * than an Exception, so that took the app down rather than failing the
     * import.
     */
    private fun isWorthCentring(
        blobWidth: Int,
        blobHeight: Int,
        sourceWidth: Int,
        sourceHeight: Int,
    ): Boolean {
        if (blobWidth * SPANS_SOURCE_PERCENT >= sourceWidth * 100 &&
            blobHeight * SPANS_SOURCE_PERCENT >= sourceHeight * 100
        ) {
            return false
        }
        val side = paddedSide(blobWidth, blobHeight)
        return side.toLong() * side <= MAX_CANDIDATE_PIXELS
    }

    private fun paddedSide(blobWidth: Int, blobHeight: Int): Int =
        (maxOf(blobWidth, blobHeight) * PADDING).toInt().coerceAtLeast(1)

    /**
     * The blob copied into the middle of a larger white square.
     *
     * Cropping and clamping to the image edge would leave a blob near the border
     * off-centre, which is precisely what the Aztec and Data Matrix detectors
     * cannot cope with. Padding cannot go wrong that way, and the white border
     * doubles as the quiet zone those symbologies want.
     */
    private fun centred(source: LuminanceSource, blob: Blob): LuminanceSource {
        val width = source.width
        val height = source.height
        val luminance = source.matrix

        val side = paddedSide(blob.width, blob.height)
        val pixels = IntArray(side * side)
        pixels.fill(WHITE)

        val offsetX = (side - blob.width) / 2
        val offsetY = (side - blob.height) / 2
        for (y in 0 until blob.height) {
            val sourceY = blob.top + y
            if (sourceY < 0 || sourceY >= height) continue
            val sourceRow = sourceY * width
            val targetRow = (offsetY + y) * side + offsetX
            for (x in 0 until blob.width) {
                val sourceX = blob.left + x
                if (sourceX < 0 || sourceX >= width) continue
                val value = luminance[sourceRow + sourceX].toInt() and 0xFF
                pixels[targetRow + x] = WHITE_ALPHA or (value shl 16) or (value shl 8) or value
            }
        }
        return RGBLuminanceSource(side, side, pixels)
    }

    /** Roughly this many cells across the short side of the image. */
    private const val CELLS_ACROSS = 64
    private const val MIN_CELL_PIXELS = 8

    /** Luminance spread within a cell that counts as "there is detail here". */
    private const val CONTRAST_THRESHOLD = 60

    private const val MAX_CANDIDATES = 6
    private const val MIN_BLOB_PIXELS = 40

    /** How much white to leave around a blob, as a multiple of its longer side. */
    private const val PADDING = 1.4f

    /** A blob at least this much of the source in both directions is the source. */
    private const val SPANS_SOURCE_PERCENT = 90

    /** Ceiling on a padded candidate, matching the cap on the image itself. */
    internal const val MAX_CANDIDATE_PIXELS = 4_000_000L

    private const val WHITE_ALPHA = 0xFF000000.toInt()
    private const val WHITE = 0xFFFFFFFF.toInt()
}
