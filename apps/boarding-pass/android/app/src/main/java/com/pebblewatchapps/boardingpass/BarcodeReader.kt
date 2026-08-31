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

/**
 * Pulls the Aztec payload out of a screenshot of an airline app.
 *
 * The screenshot is mostly UI chrome, so the reader is told to expect Aztec and
 * nothing else and to try hard, two binarizers are attempted, and the image is
 * searched in overlapping windows rather than only as a whole.
 */
object BarcodeReader {

    /** No Aztec symbol could be read out of the image. */
    class NotFoundInImageException : Exception("no Aztec barcode found in the image")

    /** The image could not be read at all (gone, or not an image). */
    class UnreadableImageException(uri: Uri) : Exception("could not read the image at $uri")

    /**
     * A full-resolution phone screenshot is a few megapixels; anything past
     * this is downsampled first, which still leaves many pixels per module.
     */
    private const val MAX_PIXELS = 4_000_000

    fun read(context: Context, uri: Uri): String = decode(loadBitmap(context, uri))

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

    private val HINTS = mapOf(
        DecodeHintType.POSSIBLE_FORMATS to listOf(BarcodeFormat.AZTEC),
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

    private fun decode(bitmap: Bitmap): String {
        val pixels = IntArray(bitmap.width * bitmap.height)
        bitmap.getPixels(pixels, 0, bitmap.width, 0, 0, bitmap.width, bitmap.height)
        val source = RGBLuminanceSource(bitmap.width, bitmap.height, pixels)

        for (region in searchRegions(source)) {
            for (binarizer in BINARIZERS) {
                try {
                    return MultiFormatReader().decode(BinaryBitmap(binarizer(region)), HINTS).text
                } catch (_: NotFoundException) {
                    // Try the next binarizer, then the next region.
                }
            }
        }
        throw NotFoundInImageException()
    }

    /**
     * The whole image first, then a grid of half-overlapping square windows.
     *
     * ZXing's Aztec detector hunts for the central bullseye outwards from the
     * middle of whatever it is handed. On a phone screenshot the code sits near
     * the top and the middle of the image is empty space below it, so the
     * detector never finds it and no amount of binarizing or TRY_HARDER helps.
     * Sliding a window across the image puts the code near the middle of one of
     * them. The windows are lazy, so a code that the whole image already finds
     * costs nothing extra.
     */
    private fun searchRegions(source: LuminanceSource): Sequence<LuminanceSource> = sequence {
        yield(source)
        if (!source.isCropSupported) {
            return@sequence
        }

        val window = minOf(source.width, source.height)
        val step = (window / 2).coerceAtLeast(1)

        var top = 0
        while (true) {
            var left = 0
            while (true) {
                val width = minOf(window, source.width - left)
                val height = minOf(window, source.height - top)
                val isWholeImage = left == 0 && top == 0 &&
                    width == source.width && height == source.height
                if (width > 0 && height > 0 && !isWholeImage) {
                    yield(source.crop(left, top, width, height))
                }
                if (left + window >= source.width) break
                left += step
            }
            if (top + window >= source.height) break
            top += step
        }
    }
}
