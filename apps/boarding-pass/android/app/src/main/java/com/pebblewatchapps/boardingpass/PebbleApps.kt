package com.pebblewatchapps.boardingpass

import android.content.Context
import io.rebble.pebblekit2.client.DefaultPebbleAndroidAppPicker

/**
 * Which Pebble app on this phone is allowed to carry the boarding pass.
 *
 * PebbleKit's default is to talk to whichever app answers first, and "answers"
 * only means declaring an intent filter that any app may declare - no
 * permission, no signature check. The library's own manifest notes that the
 * permissions which used to guard this are no longer enforced, and its
 * documentation says outright that an app sending sensitive data should turn
 * the default off.
 *
 * What travels over that link is the whole BCBP string - passenger name,
 * booking reference and frequent flyer number - so it is worth naming the app
 * rather than letting one nominate itself. With one Pebble app installed there
 * is nothing to choose and the choice is made silently; with more than one the
 * user picks.
 */
object PebbleApps {

    /**
     * Stops the library accepting whoever answers first. Synchronous, and has
     * to run before anything touches the sender or the listener service.
     */
    fun requireExplicitChoice(context: Context) {
        DefaultPebbleAndroidAppPicker.getInstance(context).enableAutoSelect = false
    }

    fun installed(context: Context): List<String> =
        DefaultPebbleAndroidAppPicker.getInstance(context).getAllEligibleApps()

    suspend fun selected(context: Context): String? =
        DefaultPebbleAndroidAppPicker.getInstance(context).getCurrentlySelectedApp()

    suspend fun select(context: Context, packageName: String?) {
        DefaultPebbleAndroidAppPicker.getInstance(context).selectApp(packageName)
    }

    /**
     * Settles the choice where there is nothing to choose between.
     *
     * @return the packages the user still has to pick from, empty once settled.
     */
    suspend fun resolve(context: Context): List<String> {
        requireExplicitChoice(context)
        if (selected(context) != null) {
            return emptyList()
        }
        val installed = installed(context)
        if (installed.size == 1) {
            select(context, installed.first())
            return emptyList()
        }
        return installed
    }
}
