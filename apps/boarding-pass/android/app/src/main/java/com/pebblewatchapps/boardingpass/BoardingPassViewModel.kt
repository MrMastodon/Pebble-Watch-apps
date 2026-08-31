package com.pebblewatchapps.boardingpass

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.google.zxing.BarcodeFormat
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class BoardingPassViewModel(application: Application) : AndroidViewModel(application) {

    data class UiState(
        val label: String? = null,
        val modules: Int = 0,
        val format: BarcodeFormat? = null,
        val sourceFormat: BarcodeFormat? = null,
        /** Known to have reached the watch, so no send button is needed. */
        val onWatch: Boolean = false,
        val busy: Boolean = false,
        val message: String? = null,
        val isError: Boolean = false,
        /** Set when the watch would show a symbology the airline did not issue. */
        val substitutionToConfirm: Substitution? = null,
        /** Pebble apps the user still has to choose between; empty once settled. */
        val pebbleAppChoices: List<String> = emptyList(),
        val pebbleApp: String? = null,
        val canChangePebbleApp: Boolean = false,
    ) {
        val hasPass: Boolean get() = modules > 0
        val isSubstituted: Boolean get() = format != null && format != sourceFormat
    }

    data class Substitution(val from: BarcodeFormat, val to: BarcodeFormat)

    private val store = PassStore(application)
    private var pass: EncodedPass? = null

    private val _state = MutableStateFlow(UiState())
    val state: StateFlow<UiState> = _state.asStateFlow()

    init {
        refreshPebbleApp()
        viewModelScope.launch {
            val encoded = withContext(Dispatchers.IO) {
                store.load()?.let { stored -> runCatching { encode(stored) }.getOrNull() }
            } ?: return@launch
            pass = encoded
            _state.value = _state.value.copy(
                label = encoded.label,
                modules = encoded.modules,
                format = encoded.format,
                sourceFormat = encoded.sourceFormat,
                onWatch = withContext(Dispatchers.IO) { store.deliveredToWatch },
            )
        }
    }

    fun choosePebbleApp(packageName: String) {
        viewModelScope.launch {
            withContext(Dispatchers.IO) { PebbleApps.select(getApplication(), packageName) }
            refreshPebbleApp()
        }
    }

    fun forgetPebbleApp() {
        viewModelScope.launch {
            withContext(Dispatchers.IO) { PebbleApps.select(getApplication(), null) }
            refreshPebbleApp()
        }
    }

    private fun refreshPebbleApp() {
        viewModelScope.launch {
            val context = getApplication<Application>()
            val choices = withContext(Dispatchers.IO) { PebbleApps.resolve(context) }
            val selected = withContext(Dispatchers.IO) { PebbleApps.selected(context) }
            val installed = withContext(Dispatchers.IO) { PebbleApps.installed(context) }
            _state.value = _state.value.copy(
                pebbleAppChoices = choices,
                pebbleApp = selected,
                canChangePebbleApp = installed.size > 1,
            )
        }
    }

    /** Reads a shared or picked screenshot, stores the pass and sends it on. */
    fun import(uri: Uri) {
        if (_state.value.busy) {
            return
        }
        _state.value = _state.value.copy(
            busy = true,
            message = null,
            isError = false,
            // Whatever was being asked about is about to be replaced.
            substitutionToConfirm = null,
        )

        viewModelScope.launch {
            val encoded = try {
                withContext(Dispatchers.IO) {
                    val scanned = BarcodeReader.read(getApplication(), uri)
                    val encoded = encode(PassStore.StoredPass(scanned.text, scanned.format))
                    // Only keep the payload once it is known to be usable, so a
                    // working pass is never replaced by one the watch cannot draw.
                    store.save(scanned.text, scanned.format)
                    encoded
                }
            } catch (error: OutOfMemoryError) {
                // Decoding allocates in proportion to the image. The candidate
                // search is bounded, but a device under memory pressure can
                // still run out, and an Error would otherwise take the app down.
                fail(error)
                return@launch
            } catch (error: Exception) {
                fail(error)
                return@launch
            }

            pass = encoded
            _state.value = _state.value.copy(
                label = encoded.label,
                modules = encoded.modules,
                format = encoded.format,
                sourceFormat = encoded.sourceFormat,
                onWatch = false,
            )
            sendOrAsk()
        }
    }

    fun sendToWatch() {
        if (_state.value.busy) {
            return
        }
        _state.value = _state.value.copy(busy = true, message = null, isError = false)
        viewModelScope.launch { sendOrAsk() }
    }

    /** The user accepted a symbology the airline did not issue. */
    fun confirmSubstitution(dontAskAgain: Boolean) {
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                store.substitutionAcknowledged = true
                if (dontAskAgain) {
                    store.alwaysAllowSubstitution = true
                }
            }
            _state.value = _state.value.copy(substitutionToConfirm = null, busy = true)
            sendStoredPass()
        }
    }

    fun cancelSubstitution() {
        _state.value = _state.value.copy(
            substitutionToConfirm = null,
            busy = false,
            message = "Not sent. The pass is still saved here if you change your mind.",
            isError = false,
        )
    }

    fun deletePass() {
        if (_state.value.busy) {
            return
        }
        _state.value = _state.value.copy(busy = true, message = null, isError = false)

        viewModelScope.launch {
            withContext(Dispatchers.IO) { store.clear() }
            pass = null

            // The watch keeps its own copy so it works with the phone in a bag,
            // which also means deleting here is not enough on its own.
            val cleared = PebbleLink.sendClear(getApplication()).isSent
            withContext(Dispatchers.IO) { store.watchDeletePending = !cleared }

            _state.value = UiState(
                message = if (cleared) {
                    "Deleted from this phone and from the watch"
                } else {
                    "Deleted from this phone. The watch clears it the next time you open " +
                        "the watchapp there."
                },
            )
        }
    }

    /**
     * Sends the pass, unless the watch would show a symbology the airline did
     * not issue and the user has not agreed to that yet.
     */
    private suspend fun sendOrAsk() {
        val current = pass
        if (current == null) {
            _state.value = _state.value.copy(busy = false)
            return
        }

        val agreed = withContext(Dispatchers.IO) {
            store.substitutionAcknowledged || store.alwaysAllowSubstitution
        }
        if (current.isSubstituted && !agreed) {
            _state.value = _state.value.copy(
                busy = false,
                substitutionToConfirm = Substitution(current.sourceFormat, current.format),
            )
            return
        }
        sendStoredPass()
    }

    private suspend fun sendStoredPass() {
        val current = pass
        if (current == null) {
            _state.value = _state.value.copy(busy = false)
            return
        }

        val outcome = PebbleLink.send(getApplication(), current)
        val message = when (outcome) {
            PebbleLink.Outcome.Sent -> "Sent to the watch"
            PebbleLink.Outcome.SentAfterOpeningWatchapp ->
                "Opened Boarding Pass on the watch and sent it there"
            PebbleLink.Outcome.NoPebbleApp ->
                "Could not reach the Pebble app - is it installed and allowed to talk to this app?"
            PebbleLink.Outcome.NoWatchConnected ->
                "Saved. No watch connected, so it will go across next time the watchapp opens."
            is PebbleLink.Outcome.Failed -> "Could not send: ${outcome.reason}"
        }
        withContext(Dispatchers.IO) { store.deliveredToWatch = outcome.isSent }
        _state.value = _state.value.copy(
            busy = false,
            message = message,
            isError = !outcome.isSent,
            onWatch = outcome.isSent,
        )
    }

    private fun encode(stored: PassStore.StoredPass): EncodedPass = SymbolEncoder.encode(
        stored.payload,
        stored.format,
        Bcbp.label(stored.payload) ?: WatchSync.DEFAULT_LABEL,
    )

    private fun fail(error: Throwable) {
        // Never log the payload itself - it carries the booking reference and
        // the frequent flyer number in the clear.
        val message = when (error) {
            is BarcodeReader.NotFoundInImageException ->
                "No boarding pass barcode found in that image. Take the screenshot with the " +
                    "barcode fully visible and unobscured."
            is BarcodeReader.UnreadableImageException ->
                "That image could not be opened."
            is OutOfMemoryError ->
                "That image was too large for this phone to process. Try a plain screenshot."
            is SymbolEncoder.TooLargeException ->
                "This boarding pass needs ${error.modules} modules, more than the watch can show legibly."
            else -> "Could not read that image: ${error.javaClass.simpleName}"
        }
        _state.value = _state.value.copy(busy = false, message = message, isError = true)
    }
}
