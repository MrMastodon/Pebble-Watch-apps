package com.pebblewatchapps.boardingpass

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
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
        val busy: Boolean = false,
        val message: String? = null,
        val isError: Boolean = false,
    ) {
        val hasPass: Boolean get() = modules > 0
    }

    private val store = PassStore(application)
    private var pass: EncodedPass? = null

    private val _state = MutableStateFlow(UiState())
    val state: StateFlow<UiState> = _state.asStateFlow()

    init {
        viewModelScope.launch {
            val stored = withContext(Dispatchers.IO) { store.load() } ?: return@launch
            val encoded = runCatching { encode(stored) }.getOrNull() ?: return@launch
            pass = encoded
            _state.value = _state.value.copy(label = encoded.label, modules = encoded.modules)
        }
    }

    /** Reads a shared or picked screenshot, stores the pass and sends it on. */
    fun import(uri: Uri) {
        if (_state.value.busy) {
            return
        }
        _state.value = _state.value.copy(busy = true, message = null, isError = false)

        viewModelScope.launch {
            val encoded = try {
                withContext(Dispatchers.IO) {
                    val payload = BarcodeReader.read(getApplication(), uri)
                    val encoded = encode(payload)
                    // Only keep the payload once it is known to be usable, so a
                    // working pass is never replaced by one the watch cannot draw.
                    store.save(payload)
                    encoded
                }
            } catch (error: Exception) {
                fail(error)
                return@launch
            }

            pass = encoded
            _state.value = _state.value.copy(label = encoded.label, modules = encoded.modules)
            sendStoredPass()
        }
    }

    fun sendToWatch() {
        if (_state.value.busy) {
            return
        }
        _state.value = _state.value.copy(busy = true, message = null, isError = false)
        viewModelScope.launch { sendStoredPass() }
    }

    fun deletePass() {
        viewModelScope.launch {
            withContext(Dispatchers.IO) { store.clear() }
            pass = null
            _state.value = UiState(message = "Boarding pass deleted from this phone")
        }
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
            PebbleLink.Outcome.NoPebbleApp ->
                "Could not reach the Pebble app - is it installed and allowed to talk to this app?"
            PebbleLink.Outcome.NoWatchConnected ->
                "Saved. No watch connected, so it will go across next time the watchapp opens."
            is PebbleLink.Outcome.Failed -> "Could not send: ${outcome.reason}"
        }
        _state.value = _state.value.copy(
            busy = false,
            message = message,
            isError = outcome != PebbleLink.Outcome.Sent,
        )
    }

    private fun encode(payload: String): EncodedPass =
        AztecEncoder.encode(payload, Bcbp.label(payload) ?: DEFAULT_LABEL)

    private fun fail(error: Exception) {
        // Never log the payload itself - it carries the booking reference and
        // the frequent flyer number in the clear.
        val message = when (error) {
            is BarcodeReader.NotFoundInImageException ->
                "No Aztec barcode found in that image. Take the screenshot with the barcode fully visible and unobscured."
            is BarcodeReader.UnreadableImageException ->
                "That image could not be opened."
            is AztecEncoder.TooLargeException ->
                "This boarding pass needs ${error.modules} modules, more than the watch can show legibly."
            else -> "Could not read that image: ${error.javaClass.simpleName}"
        }
        _state.value = _state.value.copy(busy = false, message = message, isError = true)
    }

    private companion object {
        const val DEFAULT_LABEL = "Boarding pass"
    }
}
