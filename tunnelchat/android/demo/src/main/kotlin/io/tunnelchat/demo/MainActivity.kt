package io.tunnelchat.demo

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.text.method.ScrollingMovementMethod
import android.view.Gravity
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import io.tunnelchat.Tunnelchat
import io.tunnelchat.TunnelchatConfig
import io.tunnelchat.api.ConnectionState
import io.tunnelchat.api.Message
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class MainActivity : AppCompatActivity() {

    private lateinit var tc: Tunnelchat

    private lateinit var statusView: TextView
    private lateinit var pairButton: Button
    private lateinit var connectButton: Button
    private lateinit var disconnectButton: Button
    private lateinit var textInput: EditText
    private lateinit var sendTextButton: Button
    private lateinit var pickBlobButton: Button
    private lateinit var statsView: TextView
    private lateinit var incomingView: TextView
    private lateinit var logView: TextView

    private val timeFmt = SimpleDateFormat("HH:mm:ss", Locale.US)

    private val permRequest = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { /* user will retry — we just re-check before each BLE op */ }

    private val pickBlob = registerForActivityResult(
        ActivityResultContracts.GetContent()
    ) { uri: Uri? ->
        if (uri != null) lifecycleScope.launch { sendBlobFromUri(uri) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        tc = Tunnelchat(applicationContext, TunnelchatConfig(archivePath = filesDir, debugMode = true))
        setContentView(buildUi())
        wireFlows()
        requestBlePermissions()
    }

    override fun onDestroy() {
        super.onDestroy()
        tc.close()
    }

    private fun buildUi(): View {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(24, 24, 24, 24)
        }

        statusView = TextView(this).apply {
            text = "Disconnected"
            setTypeface(typeface, Typeface.BOLD)
        }
        root.addView(statusView)

        val buttonRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        pairButton = Button(this).apply {
            text = "Pair"
            setOnClickListener { showBondedDevicesDialog() }
        }
        connectButton = Button(this).apply {
            text = "Connect"
            setOnClickListener { lifecycleScope.launch { doConnect() } }
        }
        disconnectButton = Button(this).apply {
            text = "Disconnect"
            setOnClickListener { lifecycleScope.launch { tc.disconnect() } }
        }
        buttonRow.addView(pairButton)
        buttonRow.addView(connectButton)
        buttonRow.addView(disconnectButton)
        root.addView(buttonRow)

        textInput = EditText(this).apply {
            hint = "text message (≤99 bytes)"
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
        }
        root.addView(textInput)

        val sendRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        sendTextButton = Button(this).apply {
            text = "Send text"
            setOnClickListener { lifecycleScope.launch { doSendText() } }
        }
        pickBlobButton = Button(this).apply {
            text = "Send blob"
            setOnClickListener { pickBlob.launch("*/*") }
        }
        sendRow.addView(sendTextButton)
        sendRow.addView(pickBlobButton)
        root.addView(sendRow)

        statsView = TextView(this).apply {
            text = "stats: —"
            textSize = 12f
        }
        root.addView(sectionHeader("Stats"))
        root.addView(statsView)

        root.addView(sectionHeader("Incoming"))
        incomingView = TextView(this).apply {
            textSize = 12f
            movementMethod = ScrollingMovementMethod()
        }
        root.addView(scrollable(incomingView, heightDp = 160))

        root.addView(sectionHeader("Log"))
        logView = TextView(this).apply {
            textSize = 10f
            movementMethod = ScrollingMovementMethod()
        }
        root.addView(scrollable(logView, heightDp = 200))

        return ScrollView(this).apply { addView(root) }
    }

    private fun sectionHeader(title: String) = TextView(this).apply {
        text = title
        setTypeface(typeface, Typeface.BOLD)
        setPadding(0, 24, 0, 4)
        gravity = Gravity.START
    }

    private fun scrollable(child: View, heightDp: Int): View {
        val density = resources.displayMetrics.density
        val sv = ScrollView(this)
        sv.addView(child)
        sv.layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, (heightDp * density).toInt())
        return sv
    }

    private fun wireFlows() {
        tc.connectionState
            .onEach { statusView.text = "State: ${renderState(it)}" }
            .launchIn(lifecycleScope)

        tc.incomingMessages
            .onEach { env ->
                val ts = timeFmt.format(Date(env.receivedAtMs))
                val body = when (val m = env.message) {
                    is Message.Text -> runCatching { String(m.bytes, Charsets.UTF_8) }.getOrDefault("<${m.bytes.size} bytes>")
                    is Message.Location -> "loc(${m.nodeA},${m.nodeB})"
                    is Message.Opaque -> "opaque(type=0x%02x, ${m.payload.size}B)".format(m.msgType.toInt())
                }
                appendLine(incomingView, "[$ts] ${env.senderId.raw}#${env.seq.raw}: $body")
            }
            .launchIn(lifecycleScope)

        tc.incomingBlobs
            .onEach { a ->
                val ts = timeFmt.format(Date())
                appendLine(incomingView, "[$ts] blob from ${a.senderId.raw}: ${a.bytes.size}B")
            }
            .launchIn(lifecycleScope)

        tc.statistics
            .onEach { s ->
                statsView.text = "txt out=${s.textMessagesOut}  in=${s.textMessagesIn}  " +
                        "blob chunks out=${s.blobChunksOut} in=${s.blobChunksIn}  " +
                        "proto out=${s.protoOut} in=${s.protoIn}  " +
                        "ble rcns=${s.bleReconnects}  echo rtt p50=${s.echoRttMsP50 ?: "—"}"
            }
            .launchIn(lifecycleScope)

        tc.diagnosticLog.entries
            .onEach { e ->
                val ts = timeFmt.format(Date(e.tsMs))
                appendLine(logView, "[$ts] ${e.level.name.first()} ${e.tag}: ${e.message}")
            }
            .launchIn(lifecycleScope)
    }

    private fun renderState(s: ConnectionState): String = when (s) {
        ConnectionState.Disconnected -> "Disconnected"
        is ConnectionState.Connecting -> "Connecting → ${safeName(s.device)}"
        is ConnectionState.Connected -> "Connected (mtu=${s.mtu}) ${safeName(s.device)}"
        is ConnectionState.Reconnecting -> "Reconnecting (${s.attempt})"
        is ConnectionState.Error -> "Error: ${s.err}"
        is ConnectionState.Scanning -> "Scanning"
    }

    private fun appendLine(view: TextView, line: String) {
        view.append(line + "\n")
        // Auto-scroll to bottom.
        (view.parent as? ScrollView)?.post { (view.parent as ScrollView).fullScroll(View.FOCUS_DOWN) }
    }

    // ── Pairing / connection ────────────────────────────────────────────────

    private fun bluetoothAdapter(): BluetoothAdapter? =
        (getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter

    private fun showBondedDevicesDialog() {
        if (!hasBlePermissions()) {
            requestBlePermissions()
            toast("Grant BLE permissions first")
            return
        }
        val adapter = bluetoothAdapter() ?: run { toast("No Bluetooth adapter"); return }
        val bonded = try { adapter.bondedDevices.toList() } catch (t: SecurityException) { emptyList() }
        if (bonded.isEmpty()) {
            toast("No bonded devices. Bond a tunnelchat device in Android Settings first.")
            return
        }
        val labels = bonded.map { d -> "${d.name ?: "?"}\n${d.address}" }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle("Pick bonded device")
            .setItems(labels) { _, i -> lifecycleScope.launch { doPair(bonded[i]) } }
            .show()
    }

    private suspend fun doPair(device: BluetoothDevice) {
        val r = tc.pair(device)
        toast(if (r.isSuccess) "Paired: ${safeName(device)}" else "Pair failed: ${r.exceptionOrNull()}")
    }

    private suspend fun doConnect() {
        val r = tc.connect()
        if (r.isFailure) toast("Connect failed: ${r.exceptionOrNull()}")
    }

    private suspend fun doSendText() {
        val text = textInput.text.toString()
        if (text.isEmpty()) return
        val r = tc.sendText(text.toByteArray(Charsets.UTF_8))
        if (r.isSuccess) {
            textInput.text.clear()
            val ts = timeFmt.format(Date())
            appendLine(incomingView, "[$ts] me: $text")
        } else {
            toast("sendText failed: ${r.exceptionOrNull()}")
        }
    }

    private suspend fun sendBlobFromUri(uri: Uri) {
        val bytes = contentResolver.openInputStream(uri)?.use { it.readBytes() }
            ?: run { toast("Could not read file"); return }
        if (bytes.isEmpty()) { toast("File is empty"); return }
        val handle = tc.sendBlob(bytes)
        toast("Queued blob ${bytes.size} B (id=${handle.blobId.raw})")
    }

    private fun safeName(device: BluetoothDevice): String = try {
        device.name ?: device.address
    } catch (_: SecurityException) { device.address }

    // ── Permissions ─────────────────────────────────────────────────────────

    private fun requiredBlePerms(): Array<String> =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.BLUETOOTH_SCAN)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }

    private fun hasBlePermissions(): Boolean = requiredBlePerms().all { p ->
        ContextCompat.checkSelfPermission(this, p) == PackageManager.PERMISSION_GRANTED
    }

    private fun requestBlePermissions() {
        val missing = requiredBlePerms().filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isNotEmpty()) permRequest.launch(missing.toTypedArray())
    }

    private fun toast(msg: String) = Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
}
