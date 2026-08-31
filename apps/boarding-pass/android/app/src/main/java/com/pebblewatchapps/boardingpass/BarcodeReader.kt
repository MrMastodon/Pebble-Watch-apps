package com.pebblewatchapps.boardingpass

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import com.google.zxing.BarcodeFormat
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
 * nothing else and to try hard, and two binarizers are attempted: HybridBinarizer
 * handles the usual case, while GlobalHistogramBinarizer copes better with the
 * flat, evenly lit rendering a screenshot actually is.
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

    private fun decode(bitmap: Bitmap): String {
        val pixels = IntArray(bitmap.width * bitmap.height)
        bitmap.getPixels(pixels, 0, bitmap.width, 0, 0, bitmap.width, bitmap.height)
        val source = RGBLuminanceSource(bitmap.width, bitmap.height, pixels)

        val hints = mapOf(
            DecodeHintType.POSSIBLE_FORMATS to listOf(BarcodeFormat.AZTEC),
            DecodeHintType.TRY_HARDER to true,
        )

        val binarizers = listOf<(LuminanceSource) -> BinaryBitmap>(
            { BinaryBitmap(HybridBinarizer(it)) },
            { BinaryBitmap(GlobalHistogramBinarizer(it)) },
        )

        for (binarizer in binarizers) {
            try {
                return MultiFormatReader().decode(binarizer(source), hints).text
            } catch (_: NotFoundException) {
                // Try the next binarizer.
            }
        }
        throw NotFoundInImageException()
    }
}
