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
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
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
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.core.content.IntentCompat

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
                    "${state.modules}x${state.modules} modules, " +
                        "${(state.modules * state.modules + 7) / 8} bytes on the watch",
                    style = MaterialTheme.typography.bodyMedium,
                )
            } else {
                Text(
                    "Share a screenshot of your boarding pass barcode with this app, " +
                        "or pick one below. The barcode is decoded here on the phone and " +
                        "only the finished pattern is sent to the watch.",
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

            if (state.hasPass) {
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
                    Text("Delete boarding pass from this phone")
                }
            }
        }
    }
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
