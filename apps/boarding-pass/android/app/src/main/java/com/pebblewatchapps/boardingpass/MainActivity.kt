package com.pebblewatchapps.boardingpass

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.core.content.IntentCompat
import com.google.zxing.BarcodeFormat

class MainActivity : ComponentActivity() {

    private val viewModel: BoardingPassViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            BoardingPassTheme {
                BoardingPassScreen(viewModel)
            }
        }
        importFrom(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        importFrom(intent)
    }

    private fun importFrom(intent: Intent?) {
        if (intent == null || intent.action != Intent.ACTION_SEND) {
            return
        }
        val uri = IntentCompat.getParcelableExtra(intent, Intent.EXTRA_STREAM, Uri::class.java)
            ?: return
        // Clear the action so a rotation does not import the same image again.
        intent.action = null
        viewModel.import(uri)
    }
}

@Composable
private fun BoardingPassTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = if (isSystemInDarkTheme()) darkColorScheme() else lightColorScheme(),
        content = content,
    )
}

@Composable
private fun BoardingPassScreen(viewModel: BoardingPassViewModel) {
    val state by viewModel.state.collectAsState()
    val picker = rememberScreenshotPicker(viewModel)

    state.substitutionToConfirm?.let { substitution ->
        SubstitutionDialog(
            substitution = substitution,
            onConfirm = viewModel::confirmSubstitution,
            onDismiss = viewModel::cancelSubstitution,
        )
    }

    Scaffold { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(24.dp)
                .verticalScroll(rememberScrollState()),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Text("Boarding Pass", style = MaterialTheme.typography.headlineMedium)

            if (state.hasPass) {
                Text(state.label.orEmpty(), style = MaterialTheme.typography.displaySmall)
                Text(
                    "${symbologyName(state.format)}, ${state.modules}x${state.modules} modules, " +
                        "${(state.modules * state.modules + 7) / 8} bytes on the watch",
                    style = MaterialTheme.typography.bodyMedium,
                )
                Text(
                    if (state.onWatch) "On the watch" else "Not on the watch yet",
                    style = MaterialTheme.typography.bodyMedium,
                )
                if (state.isSubstituted) {
                    Text(
                        "Read as ${symbologyName(state.sourceFormat)}, which is too wide to draw " +
                            "on the watch. The watch shows the same boarding pass data as " +
                            "${symbologyName(state.format)} instead.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.error,
                    )
                }
            } else {
                Text(
                    "Share a screenshot of your boarding pass barcode with this app, " +
                        "or pick one below. Aztec, QR, Data Matrix and PDF417 are all read. " +
                        "The barcode is decoded here on the phone and only the finished " +
                        "pattern is sent to the watch.",
                    style = MaterialTheme.typography.bodyLarge,
                )
            }

            Spacer(Modifier.height(8.dp))

            Button(
                onClick = { picker() },
                enabled = !state.busy,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text(if (state.hasPass) "Pick another screenshot" else "Pick a screenshot")
            }

            // Sharing a screenshot sends it straight to the watch, so this is
            // only here for the cases where that did not happen: the watch was
            // out of reach, or a substitution was declined and reconsidered.
            if (state.hasPass && !state.onWatch) {
                Button(
                    onClick = { viewModel.sendToWatch() },
                    enabled = !state.busy,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text("Send to watch")
                }
            }

            if (state.busy) {
                CircularProgressIndicator()
            }

            state.message?.let { message ->
                Text(
                    message,
                    style = MaterialTheme.typography.bodyMedium,
                    color = if (state.isError) {
                        MaterialTheme.colorScheme.error
                    } else {
                        MaterialTheme.colorScheme.onSurfaceVariant
                    },
                )
            }

            if (state.hasPass) {
                Spacer(Modifier.height(8.dp))
                TextButton(onClick = { viewModel.deletePass() }, enabled = !state.busy) {
                    Text("Delete boarding pass from phone and watch")
                }
            }
        }
    }
}

/**
 * Asks before the watch shows a symbology the airline did not issue.
 *
 * IATA allows all four 2D symbologies on a boarding pass and gate readers are
 * imaging scanners, so the swap should be invisible to them - but "should" is
 * not something to discover at a gate, so the choice is the user's.
 */
@Composable
private fun SubstitutionDialog(
    substitution: BoardingPassViewModel.Substitution,
    onConfirm: (Boolean) -> Unit,
    onDismiss: () -> Unit,
) {
    var dontAskAgain by remember { mutableStateOf(false) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Show it as ${symbologyName(substitution.to)}?") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text(
                    "This pass is a ${symbologyName(substitution.from)} code, which is far too " +
                        "wide to draw on the watch. The watch can show the same boarding pass " +
                        "data as ${symbologyName(substitution.to)} instead."
                )
                Text(
                    "Airlines are allowed to issue either, and gate scanners read both, but " +
                        "this is not the symbol your airline printed. Check that it scans " +
                        "before you rely on it."
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Checkbox(checked = dontAskAgain, onCheckedChange = { dontAskAgain = it })
                    Spacer(Modifier.width(8.dp))
                    Text("Do not ask me again")
                }
            }
        },
        confirmButton = {
            TextButton(onClick = { onConfirm(dontAskAgain) }) { Text("Send to watch") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}

private fun symbologyName(format: BarcodeFormat?): String = when (format) {
    BarcodeFormat.AZTEC -> "Aztec"
    BarcodeFormat.QR_CODE -> "QR"
    BarcodeFormat.DATA_MATRIX -> "Data Matrix"
    BarcodeFormat.PDF_417 -> "PDF417"
    else -> "unknown"
}

/**
 * The system photo picker, so an older screenshot can be imported without going
 * back through the share sheet.
 */
@Composable
private fun rememberScreenshotPicker(viewModel: BoardingPassViewModel): () -> Unit {
    val launcher = rememberLauncherForActivityResult(
        ActivityResultContracts.PickVisualMedia()
    ) { uri -> uri?.let { viewModel.import(it) } }

    return {
        launcher.launch(
            PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly)
        )
    }
}
