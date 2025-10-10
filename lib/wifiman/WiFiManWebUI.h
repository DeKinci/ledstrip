#ifndef WIFIMAN_WEBUI_H
#define WIFIMAN_WEBUI_H

namespace WiFiMan {

// Captive portal HTML page
const char WIFIMAN_PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WiFi Setup</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 12px;
            box-shadow: 0 10px 40px rgba(0,0,0,0.2);
            max-width: 500px;
            width: 100%;
            padding: 30px;
        }
        h1 {
            color: #333;
            margin-bottom: 10px;
            font-size: 24px;
        }
        .subtitle {
            color: #666;
            margin-bottom: 25px;
            font-size: 14px;
        }
        .section {
            margin-bottom: 25px;
        }
        .section-title {
            font-weight: 600;
            color: #444;
            margin-bottom: 12px;
            font-size: 16px;
        }
        .network-list {
            border: 1px solid #ddd;
            border-radius: 8px;
            overflow: hidden;
            margin-bottom: 15px;
            max-height: 300px;
            overflow-y: auto;
        }
        .network-item {
            padding: 12px 15px;
            border-bottom: 1px solid #eee;
            display: flex;
            justify-content: space-between;
            align-items: center;
            cursor: pointer;
            transition: background 0.2s;
        }
        .network-item:hover {
            background: #f5f5f5;
        }
        .network-item:last-child {
            border-bottom: none;
        }
        .network-name {
            font-weight: 500;
            color: #333;
        }
        .network-rssi {
            font-size: 12px;
            color: #666;
        }
        .signal-strong { color: #22c55e; }
        .signal-medium { color: #f59e0b; }
        .signal-weak { color: #ef4444; }
        input[type="text"], input[type="password"], input[type="number"] {
            width: 100%;
            padding: 12px;
            border: 1px solid #ddd;
            border-radius: 6px;
            font-size: 14px;
            margin-bottom: 12px;
            transition: border 0.3s;
        }
        input:focus {
            outline: none;
            border-color: #667eea;
        }
        button {
            width: 100%;
            padding: 14px;
            background: #667eea;
            color: white;
            border: none;
            border-radius: 6px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: background 0.3s;
        }
        button:hover {
            background: #5568d3;
        }
        button:disabled {
            background: #ccc;
            cursor: not-allowed;
        }
        .btn-secondary {
            background: #6c757d;
            margin-top: 10px;
        }
        .btn-secondary:hover {
            background: #5a6268;
        }
        .btn-danger {
            background: #ef4444;
            margin-top: 10px;
        }
        .btn-danger:hover {
            background: #dc2626;
        }
        .message {
            padding: 12px;
            border-radius: 6px;
            margin-bottom: 15px;
            display: none;
        }
        .message.success {
            background: #d1fae5;
            color: #065f46;
            border: 1px solid #6ee7b7;
        }
        .message.error {
            background: #fee2e2;
            color: #991b1b;
            border: 1px solid #fca5a5;
        }
        .saved-networks {
            margin-top: 20px;
        }
        .saved-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 10px 15px;
            background: #f9fafb;
            border-radius: 6px;
            margin-bottom: 8px;
        }
        .saved-item button {
            width: auto;
            padding: 6px 12px;
            font-size: 12px;
        }
        .loading {
            text-align: center;
            padding: 20px;
            color: #666;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>WiFi Configuration</h1>
        <p class="subtitle">Configure your device's network settings</p>

        <div id="message" class="message"></div>

        <div class="section">
            <div class="section-title">Available Networks</div>
            <div class="network-list" id="networkList">
                <div class="loading">Scanning for networks...</div>
            </div>
            <button onclick="scanNetworks()" class="btn-secondary">Refresh Networks</button>
        </div>

        <div class="section">
            <div class="section-title">Add Network</div>
            <input type="text" id="ssid" placeholder="Network Name (SSID)" />
            <input type="password" id="password" placeholder="Password" />
            <input type="number" id="priority" placeholder="Priority (0-100)" value="50" min="0" max="100" />
            <button onclick="addNetwork()">Add Network</button>
        </div>

        <div class="section saved-networks">
            <div class="section-title">Saved Networks</div>
            <div id="savedNetworks">
                <div class="loading">Loading...</div>
            </div>
        </div>

        <div class="section">
            <button onclick="connectNow()" class="btn-secondary">Connect Now</button>
            <button onclick="clearAll()" class="btn-danger">Clear All Networks</button>
        </div>
    </div>

    <script>
        function showMessage(msg, isError = false) {
            const el = document.getElementById('message');
            el.textContent = msg;
            el.className = 'message ' + (isError ? 'error' : 'success');
            el.style.display = 'block';
            setTimeout(() => { el.style.display = 'none'; }, 5000);
        }

        async function scanNetworks() {
            const list = document.getElementById('networkList');
            list.innerHTML = '<div class="loading">Scanning...</div>';

            try {
                console.log('Fetching /wifiman/scan');
                const res = await fetch('/wifiman/scan');
                console.log('Response status:', res.status);
                const data = await res.json();
                console.log('Response data:', data);

                if (data.status === 'scanning') {
                    // Scan in progress, retry after 2 seconds
                    setTimeout(scanNetworks, 2000);
                    return;
                }

                if (data.networks && data.networks.length > 0) {
                    list.innerHTML = data.networks.map((net, idx) => `
                        <div class="network-item" onclick="selectNetwork(${idx})">
                            <span class="network-name">${escapeHtml(net.ssid)}</span>
                            <span class="network-rssi signal-${getRSSIClass(net.rssi)}">${getRSSIIcon(net.rssi)} ${net.rssi} dBm</span>
                        </div>
                    `).join('');
                    window.scannedNetworks = data.networks; // Store for selectNetwork
                } else {
                    list.innerHTML = '<div class="loading">No networks found</div>';
                }
            } catch (e) {
                console.error('Scan error:', e);
                list.innerHTML = '<div class="loading">Error scanning: ' + e.message + '</div>';
            }
        }

        function getRSSIClass(rssi) {
            if (rssi > -60) return 'strong';
            if (rssi > -75) return 'medium';
            return 'weak';
        }

        function getRSSIIcon(rssi) {
            if (rssi > -60) return '▂▄▆█';
            if (rssi > -75) return '▂▄▆';
            return '▂▄';
        }

        function escapeHtml(text) {
            const div = document.createElement('div');
            div.textContent = text;
            return div.innerHTML;
        }

        function selectNetwork(index) {
            if (window.scannedNetworks && window.scannedNetworks[index]) {
                document.getElementById('ssid').value = window.scannedNetworks[index].ssid;
                document.getElementById('password').focus();
            }
        }

        async function addNetwork() {
            const ssid = document.getElementById('ssid').value;
            const password = document.getElementById('password').value;
            const priority = document.getElementById('priority').value;

            if (!ssid) {
                showMessage('Please enter a network name', true);
                return;
            }

            try {
                const res = await fetch('/wifiman/add', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ ssid, password, priority: parseInt(priority) })
                });

                if (res.ok) {
                    showMessage('Network added successfully!');
                    document.getElementById('ssid').value = '';
                    document.getElementById('password').value = '';
                    document.getElementById('priority').value = '50';
                    loadSavedNetworks();
                } else {
                    showMessage('Failed to add network', true);
                }
            } catch (e) {
                showMessage('Error: ' + e.message, true);
            }
        }

        async function loadSavedNetworks() {
            const container = document.getElementById('savedNetworks');

            try {
                console.log('Fetching /wifiman/list');
                const res = await fetch('/wifiman/list');
                console.log('List response status:', res.status);
                const data = await res.json();
                console.log('List response data:', data);

                if (data.networks && data.networks.length > 0) {
                    container.innerHTML = data.networks.map(net => `
                        <div class="saved-item">
                            <div>
                                <strong>${net.ssid}</strong>
                                <span style="font-size: 12px; color: #666;"> (Priority: ${net.priority})</span>
                            </div>
                            <button onclick="removeNetwork('${net.ssid}')">Remove</button>
                        </div>
                    `).join('');
                } else {
                    container.innerHTML = '<div class="loading">No saved networks</div>';
                }
            } catch (e) {
                console.error('List error:', e);
                container.innerHTML = '<div class="loading">Error loading networks: ' + e.message + '</div>';
            }
        }

        async function removeNetwork(ssid) {
            if (!confirm(`Remove network "${ssid}"?`)) return;

            try {
                const res = await fetch('/wifiman/remove', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ ssid })
                });

                if (res.ok) {
                    showMessage('Network removed');
                    loadSavedNetworks();
                } else {
                    showMessage('Failed to remove network', true);
                }
            } catch (e) {
                showMessage('Error: ' + e.message, true);
            }
        }

        async function clearAll() {
            if (!confirm('Clear all saved networks? This cannot be undone.')) return;

            try {
                const res = await fetch('/wifiman/clear', { method: 'POST' });
                if (res.ok) {
                    showMessage('All networks cleared');
                    loadSavedNetworks();
                } else {
                    showMessage('Failed to clear networks', true);
                }
            } catch (e) {
                showMessage('Error: ' + e.message, true);
            }
        }

        async function connectNow() {
            try {
                const res = await fetch('/wifiman/connect', { method: 'POST' });
                if (res.ok) {
                    showMessage('Attempting to connect...');
                    setTimeout(() => {
                        showMessage('Device is connecting. You may need to reconnect to your network.');
                    }, 2000);
                } else {
                    showMessage('Failed to start connection', true);
                }
            } catch (e) {
                showMessage('Error: ' + e.message, true);
            }
        }

        // Initial load
        scanNetworks();
        loadSavedNetworks();
        setInterval(loadSavedNetworks, 10000);
    </script>
</body>
</html>
)rawliteral";

} // namespace WiFiMan

#endif // WIFIMAN_WEBUI_H
