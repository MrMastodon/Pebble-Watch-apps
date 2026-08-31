package com.pebblewatchapps.boardingpass

import android.app.Application
import androidx.test.core.app.ApplicationProvider
import io.rebble.pebblekit2.client.DefaultPebbleAndroidAppPicker
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/**
 * PebbleKit will otherwise talk to whichever app answers first, and answering
 * takes nothing but an intent filter any app can declare - the library's own
 * manifest says the permissions that used to guard it are no longer enforced.
 * What crosses that link is the whole BCBP string, so this must stay off.
 *
 * It is easy to lose: delete android:name from the manifest, or the call from
 * onCreate, and the app silently goes back to trusting anyone.
 */
@RunWith(RobolectricTestRunner::class)
class PebbleAppsTest {

    @Test
    fun `the app class that locks the choice down is actually registered`() {
        val application = ApplicationProvider.getApplicationContext<Application>()

        assertTrue(
            "expected BoardingPassApplication, got ${application.javaClass.name}",
            application is BoardingPassApplication,
        )
    }

    @Test
    fun `the library never picks a pebble app on its own`() {
        val application = ApplicationProvider.getApplicationContext<Application>()

        assertFalse(DefaultPebbleAndroidAppPicker.getInstance(application).enableAutoSelect)
    }
}
