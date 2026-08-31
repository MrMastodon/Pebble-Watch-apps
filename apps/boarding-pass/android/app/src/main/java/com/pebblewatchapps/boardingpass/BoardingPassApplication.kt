package com.pebblewatchapps.boardingpass

import android.app.Application
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/**
 * Exists for one reason: to name the Pebble app before anything can talk to it.
 *
 * The listener service can be started by the Pebble app without MainActivity
 * ever having run, so this cannot wait for a screen to open.
 */
class BoardingPassApplication : Application() {

    override fun onCreate() {
        super.onCreate()
        PebbleApps.requireExplicitChoice(this)
        CoroutineScope(SupervisorJob() + Dispatchers.IO).launch {
            PebbleApps.resolve(this@BoardingPassApplication)
        }
    }
}
