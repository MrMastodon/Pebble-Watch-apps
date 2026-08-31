package com.pebblewatchapps.boardingpass

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.security.KeyStore
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey

/**
 * Where the key that encrypts the stored boarding pass comes from.
 *
 * This is an interface only because the Android Keystore does not exist off a
 * device, and the rest of PassStore - the symbology round-trip, what a delete
 * clears and what it deliberately does not - is worth testing on the JVM.
 */
interface PassKey {
    fun secretKey(): SecretKey

    /** Throws the key away, making anything encrypted under it unreadable. */
    fun discard()
}

/**
 * The real one: an AES-GCM key generated inside the Android Keystore, which
 * never leaves it and is not included in backups.
 */
class AndroidKeystorePassKey : PassKey {

    override fun secretKey(): SecretKey {
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

    override fun discard() {
        runCatching { keyStore().deleteEntry(KEY_ALIAS) }
    }

    private fun keyStore(): KeyStore = KeyStore.getInstance(KEYSTORE).apply { load(null) }

    private companion object {
        const val KEYSTORE = "AndroidKeyStore"
        const val KEY_ALIAS = "boarding_pass_payload"
    }
}
