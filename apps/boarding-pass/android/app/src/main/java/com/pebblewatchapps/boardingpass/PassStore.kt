package com.pebblewatchapps.boardingpass

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import androidx.core.content.edit
import java.security.GeneralSecurityException
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
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
class PassStore(context: Context) {

    private val preferences =
        context.applicationContext.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)

    fun save(payload: String) {
        val cipher = Cipher.getInstance(TRANSFORMATION).apply { init(Cipher.ENCRYPT_MODE, secretKey()) }
        val ciphertext = cipher.doFinal(payload.toByteArray(Charsets.UTF_8))
        val stored = cipher.iv + ciphertext
        preferences.edit { putString(KEY_PAYLOAD, Base64.encodeToString(stored, Base64.NO_WRAP)) }
    }

    fun load(): String? {
        val stored = preferences.getString(KEY_PAYLOAD, null) ?: return null
        return try {
            val bytes = Base64.decode(stored, Base64.NO_WRAP)
            val cipher = Cipher.getInstance(TRANSFORMATION).apply {
                init(
                    Cipher.DECRYPT_MODE,
                    secretKey(),
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
        preferences.edit { remove(KEY_PAYLOAD) }
        runCatching { keyStore().deleteEntry(KEY_ALIAS) }
    }

    private fun keyStore(): KeyStore = KeyStore.getInstance(KEYSTORE).apply { load(null) }

    private fun secretKey(): SecretKey {
        val existing = keyStore().getEntry(KEY_ALIAS, null) as? KeyStore.SecretKeyEntry
        if (existing != null) {
            return existing.secretKey
        }
        val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE)
        generator.init(
            KeyGenParameterSpec.Builder(
                KEY_ALIAS,
                KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
            )
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setRandomizedEncryptionRequired(true)
                .build()
        )
        return generator.generateKey()
    }

    private companion object {
        const val PREFERENCES_NAME = "boarding_pass"
        const val KEY_PAYLOAD = "payload"
        const val KEYSTORE = "AndroidKeyStore"
        const val KEY_ALIAS = "boarding_pass_payload"
        const val TRANSFORMATION = "AES/GCM/NoPadding"
        const val IV_LENGTH_BYTES = 12
        const val TAG_LENGTH_BITS = 128
    }
}
