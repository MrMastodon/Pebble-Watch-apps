package com.pebblewatchapps.boardingpass

import android.content.Context
import android.util.Base64
import androidx.core.content.edit
import com.google.zxing.BarcodeFormat
import java.security.GeneralSecurityException
import javax.crypto.Cipher
import javax.crypto.spec.GCMParameterSpec

/**
 * Keeps the last boarding pass on the phone, encrypted.
 *
 * A BCBP string carries the booking reference and the frequent flyer number in
 * the clear, so it is stored under an AES-GCM key that lives in the Android
 * Keystore and never leaves it. It is never logged, in any build.
 *
 * This is hand-rolled rather than EncryptedSharedPreferences on purpose:
 * androidx.security-crypto has been deprecated since 1.1.0-alpha07 with no
 * stable successor, and this is roughly the same amount of code without the
 * dependency.
 */
class PassStore(
    context: Context,
    private val key: PassKey = AndroidKeystorePassKey(),
) {

    private val preferences =
        context.applicationContext.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)

    /** The stored pass: what the barcode said, and which symbology said it. */
    class StoredPass(val payload: String, val format: BarcodeFormat)

    fun save(payload: String, format: BarcodeFormat) {
        // The symbology travels with the payload so the pass can be re-encoded
        // later exactly as it was read, without another look at the screenshot.
        val record = "${format.name}\n$payload"
        val cipher = Cipher.getInstance(TRANSFORMATION).apply { init(Cipher.ENCRYPT_MODE, key.secretKey()) }
        val ciphertext = cipher.doFinal(record.toByteArray(Charsets.UTF_8))
        val stored = cipher.iv + ciphertext
        preferences.edit {
            putString(KEY_PAYLOAD, Base64.encodeToString(stored, Base64.NO_WRAP))
            // A new pass has not been agreed to yet, and it supersedes any
            // delete the watch has not caught up with.
            remove(KEY_SUBSTITUTION_ACKNOWLEDGED)
            remove(KEY_WATCH_DELETE_PENDING)
            // A pass nobody has sent yet is not on the watch.
            remove(KEY_DELIVERED_TO_WATCH)
        }
    }

    fun load(): StoredPass? {
        val record = loadRecord() ?: return null
        val separator = record.indexOf('\n')
        if (separator <= 0) {
            return null
        }
        val format = runCatching { BarcodeFormat.valueOf(record.substring(0, separator)) }
            .getOrNull() ?: return null
        return StoredPass(record.substring(separator + 1), format)
    }

    /**
     * Whether the user has agreed to the stored pass being shown in a different
     * symbology than the airline issued. Cleared whenever a new pass is saved.
     */
    var substitutionAcknowledged: Boolean
        get() = preferences.getBoolean(KEY_SUBSTITUTION_ACKNOWLEDGED, false)
        set(value) = preferences.edit { putBoolean(KEY_SUBSTITUTION_ACKNOWLEDGED, value) }

    /**
     * Whether this exact pass is known to have reached the watch. Only used to
     * decide whether to offer a send button - the watch is pushed the current
     * pass whenever the watchapp opens regardless, so a wrong answer here heals
     * itself rather than stranding anything.
     */
    var deliveredToWatch: Boolean
        get() = preferences.getBoolean(KEY_DELIVERED_TO_WATCH, false)
        set(value) = preferences.edit { putBoolean(KEY_DELIVERED_TO_WATCH, value) }

    /**
     * Set when the pass was deleted here but the watch was not reachable to be
     * told. The listener service sends the deletion when the watchapp next
     * opens, so this deliberately survives [clear].
     */
    var watchDeletePending: Boolean
        get() = preferences.getBoolean(KEY_WATCH_DELETE_PENDING, false)
        set(value) = preferences.edit { putBoolean(KEY_WATCH_DELETE_PENDING, value) }

    /** The "do not ask again" answer, which outlives any single pass. */
    var alwaysAllowSubstitution: Boolean
        get() = preferences.getBoolean(KEY_ALWAYS_ALLOW_SUBSTITUTION, false)
        set(value) = preferences.edit { putBoolean(KEY_ALWAYS_ALLOW_SUBSTITUTION, value) }

    private fun loadRecord(): String? {
        val stored = preferences.getString(KEY_PAYLOAD, null) ?: return null
        return try {
            val bytes = Base64.decode(stored, Base64.NO_WRAP)
            val cipher = Cipher.getInstance(TRANSFORMATION).apply {
                init(
                    Cipher.DECRYPT_MODE,
                    key.secretKey(),
                    GCMParameterSpec(TAG_LENGTH_BITS, bytes, 0, IV_LENGTH_BYTES),
                )
            }
            String(
                cipher.doFinal(bytes, IV_LENGTH_BYTES, bytes.size - IV_LENGTH_BYTES),
                Charsets.UTF_8,
            )
        } catch (_: GeneralSecurityException) {
            // The key is gone or no longer matches the ciphertext - after a
            // restore onto a new device, say. The stored bytes are permanently
            // unreadable, so drop them and let the user share a fresh
            // screenshot rather than failing on every launch from here on.
            clear()
            null
        } catch (_: IllegalArgumentException) {
            clear()
            null
        }
    }

    fun clear() {
        preferences.edit {
            remove(KEY_PAYLOAD)
            remove(KEY_SUBSTITUTION_ACKNOWLEDGED)
            remove(KEY_DELIVERED_TO_WATCH)
        }
        key.discard()
    }

    private companion object {
        const val PREFERENCES_NAME = "boarding_pass"
        const val KEY_PAYLOAD = "payload"
        const val KEY_SUBSTITUTION_ACKNOWLEDGED = "substitution_acknowledged"
        const val KEY_ALWAYS_ALLOW_SUBSTITUTION = "always_allow_substitution"
        const val KEY_WATCH_DELETE_PENDING = "watch_delete_pending"
        const val KEY_DELIVERED_TO_WATCH = "delivered_to_watch"
        const val TRANSFORMATION = "AES/GCM/NoPadding"
        const val IV_LENGTH_BYTES = 12
        const val TAG_LENGTH_BITS = 128
    }
}
