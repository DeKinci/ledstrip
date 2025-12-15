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
        h1 { color: #333; margin-bottom: 8px; font-size: 24px; }
        .subtitle { color: #666; margin-bottom: 20px; font-size: 14px; }
        .section { margin-bottom: 20px; }
        .section-title { font-weight: 600; color: #444; margin-bottom: 10px; font-size: 14px; text-transform: uppercase; letter-spacing: 0.5px; }
        .section-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
        .section-header .section-title { margin-bottom: 0; }
        .btn-icon { padding: 6px 10px; font-size: 14px; background: transparent; color: #667eea; border: 1px solid #667eea; }
        .btn-icon:hover { background: #667eea; color: white; }

        /* Status bar */
        .status-bar {
            padding: 12px 15px;
            border-radius: 8px;
            margin-bottom: 20px;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .status-bar.connected { background: #d1fae5; }
        .status-bar.ap-mode { background: #dbeafe; }
        .status-bar.disconnected { background: #fef3c7; }
        .status-text { font-weight: 500; }
        .status-ip { font-size: 13px; color: #666; }

        /* Network list */
        .network-list {
            border: 1px solid #e5e7eb;
            border-radius: 8px;
            overflow: hidden;
            margin-bottom: 15px;
        }
        .network-item {
            padding: 12px 15px;
            border-bottom: 1px solid #f3f4f6;
            display: flex;
            justify-content: space-between;
            align-items: center;
            transition: background 0.2s;
        }
        .network-item:last-child { border-bottom: none; }
        .network-item:hover { background: #f9fafb; }
        .network-item.connected { background: #ecfdf5; }
        .network-item.not-in-range { opacity: 0.6; background: #f9fafb; }
        .network-item.clickable { cursor: pointer; }

        .network-info { flex: 1; min-width: 0; }
        .network-name { font-weight: 500; color: #333; display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
        .network-meta { font-size: 12px; color: #888; margin-top: 2px; }

        .badge {
            font-size: 10px;
            padding: 2px 6px;
            border-radius: 4px;
            font-weight: 600;
            text-transform: uppercase;
        }
        .badge-connected { background: #10b981; color: white; }
        .badge-saved { background: #e5e7eb; color: #374151; }
        .badge-open { background: #fef3c7; color: #92400e; }

        .network-actions { display: flex; gap: 8px; align-items: center; margin-left: 10px; }
        .signal { font-size: 12px; color: #888; white-space: nowrap; }
        .signal-strong { color: #22c55e; }
        .signal-medium { color: #f59e0b; }
        .signal-weak { color: #ef4444; }

        .btn-sm {
            padding: 5px 10px;
            font-size: 12px;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            font-weight: 500;
        }
        .btn-forget { background: #fee2e2; color: #dc2626; }
        .btn-forget:hover { background: #fecaca; }
        .btn-connect { background: #dbeafe; color: #2563eb; }
        .btn-connect:hover { background: #bfdbfe; }

        /* Add network form */
        .add-form { display: none; padding: 15px; background: #f9fafb; border-radius: 8px; margin-bottom: 15px; }
        .add-form.visible { display: block; }
        .add-form input {
            width: 100%;
            padding: 10px;
            border: 1px solid #ddd;
            border-radius: 6px;
            font-size: 14px;
            margin-bottom: 10px;
        }
        .add-form input:focus { outline: none; border-color: #667eea; }
        .form-row { display: flex; gap: 10px; }
        .form-row input { flex: 1; }

        button {
            padding: 12px 20px;
            background: #667eea;
            color: white;
            border: none;
            border-radius: 6px;
            font-size: 14px;
            font-weight: 600;
            cursor: pointer;
        }
        button:hover { background: #5568d3; }
        button:disabled { background: #ccc; cursor: not-allowed; }
        .btn-secondary { background: #6c757d; }
        .btn-secondary:hover { background: #5a6268; }
        .btn-full { width: 100%; }

        .btn-row { display: flex; gap: 10px; margin-top: 15px; }
        .btn-row button { flex: 1; }

        .message {
            padding: 12px;
            border-radius: 6px;
            margin-bottom: 15px;
            display: none;
        }
        .message.success { background: #d1fae5; color: #065f46; display: block; }
        .message.error { background: #fee2e2; color: #991b1b; display: block; }

        .loading { text-align: center; padding: 20px; color: #666; }
        .divider { border-top: 1px dashed #e5e7eb; margin: 8px 0; }
        .empty-state { padding: 20px; text-align: center; color: #888; font-size: 14px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>WiFi Configuration</h1>
        <p class="subtitle">Select a network to connect</p>

        <div id="message" class="message"></div>

        <div id="statusBar" class="status-bar disconnected">
            <span class="status-text">Loading...</span>
            <span class="status-ip"></span>
        </div>

        <div class="section">
            <div class="section-header">
                <div class="section-title">Networks</div>
                <button onclick="refresh()" class="btn-icon" title="Scan">↻</button>
            </div>
            <div class="network-list" id="networkList">
                <div class="loading">Scanning...</div>
            </div>
        </div>

        <div id="addForm" class="add-form">
            <div class="section-title">Connect to Network</div>
            <input type="text" id="ssidInput" placeholder="Network Name" readonly />
            <input type="password" id="passwordInput" placeholder="Password (leave empty if open)" />
            <div class="form-row">
                <input type="number" id="priorityInput" placeholder="Priority" value="50" min="0" max="100" />
            </div>
            <div class="btn-row">
                <button onclick="hideAddForm()" class="btn-secondary">Cancel</button>
                <button onclick="saveAndConnect()">Save & Connect</button>
            </div>
        </div>

        <div class="section">
            <div class="section-title">Manual Entry</div>
            <button onclick="showManualEntry()" class="btn-secondary btn-full">Add Hidden Network</button>
        </div>
    </div>

    <script>
        let scannedNetworks = [];
        let savedNetworks = [];
        let currentSSID = '';
        let isConnected = false;

        function showMessage(msg, isError = false) {
            const el = document.getElementById('message');
            el.textContent = msg;
            el.className = 'message ' + (isError ? 'error' : 'success');
            setTimeout(() => { el.className = 'message'; }, 4000);
        }

        function escapeHtml(text) {
            const div = document.createElement('div');
            div.textContent = text || '';
            return div.innerHTML;
        }

        function getRSSI(rssi) {
            const cls = rssi > -60 ? 'strong' : rssi > -75 ? 'medium' : 'weak';
            const bars = rssi > -60 ? '▂▄▆█' : rssi > -75 ? '▂▄▆░' : '▂▄░░';
            return `<span class="signal signal-${cls}">${bars} ${rssi}dBm</span>`;
        }

        async function loadStatus() {
            try {
                const res = await fetch('/wifiman/status');
                const data = await res.json();

                const bar = document.getElementById('statusBar');
                currentSSID = data.ssid || '';
                isConnected = data.connected;

                if (data.connected) {
                    bar.className = 'status-bar connected';
                    bar.innerHTML = `<span class="status-text">✓ Connected to ${escapeHtml(data.ssid)}</span><span class="status-ip">IP: ${data.ip}</span>`;
                } else if (data.apMode) {
                    bar.className = 'status-bar ap-mode';
                    let apText = `📶 AP Mode: ${escapeHtml(data.ssid || 'ESP32-Setup')}`;
                    if (data.error) {
                        apText += `<br><span style="font-size:12px;color:#dc2626">⚠ ${escapeHtml(data.error)}</span>`;
                    }
                    bar.innerHTML = `<span class="status-text">${apText}</span><span class="status-ip">IP: ${data.ip}</span>`;
                } else {
                    bar.className = 'status-bar disconnected';
                    let statusText = data.state || 'Disconnected';
                    if (data.error) {
                        statusText += `: ${escapeHtml(data.error)}`;
                    }
                    bar.innerHTML = `<span class="status-text">⏳ ${statusText}</span><span class="status-ip"></span>`;
                }
            } catch (e) {
                console.error('Status error:', e);
            }
        }

        async function loadSaved() {
            try {
                const res = await fetch('/wifiman/list');
                const data = await res.json();
                savedNetworks = data.networks || [];
            } catch (e) {
                console.error('List error:', e);
                savedNetworks = [];
            }
        }

        async function scan() {
            try {
                const res = await fetch('/wifiman/scan');
                const data = await res.json();

                if (data.status === 'scanning') {
                    setTimeout(scan, 1500);
                    return;
                }

                scannedNetworks = data.networks || [];
                renderNetworks();
            } catch (e) {
                console.error('Scan error:', e);
                scannedNetworks = [];
                renderNetworks();
            }
        }

        function isSaved(ssid) {
            return savedNetworks.some(n => n.ssid === ssid);
        }

        function getSavedPriority(ssid) {
            const net = savedNetworks.find(n => n.ssid === ssid);
            return net ? net.priority : 50;
        }

        function renderNetworks() {
            const list = document.getElementById('networkList');
            const inRange = new Set(scannedNetworks.map(n => n.ssid));

            // Build unified list
            let html = '';

            // 1. Available networks (in range), connected first
            const available = [...scannedNetworks].sort((a, b) => {
                // Connected network first
                if (a.ssid === currentSSID && isConnected) return -1;
                if (b.ssid === currentSSID && isConnected) return 1;
                // Then by signal strength
                return b.rssi - a.rssi;
            });

            if (available.length > 0) {
                available.forEach(net => {
                    const saved = isSaved(net.ssid);
                    const connected = isConnected && net.ssid === currentSSID;
                    const open = !net.encrypted;

                    html += `<div class="network-item ${connected ? 'connected' : 'clickable'}" ${!connected ? `onclick="selectNetwork('${escapeHtml(net.ssid)}', ${open})"` : ''}>
                        <div class="network-info">
                            <div class="network-name">
                                ${escapeHtml(net.ssid)}
                                ${connected ? '<span class="badge badge-connected">Connected</span>' : ''}
                                ${saved && !connected ? '<span class="badge badge-saved">Saved</span>' : ''}
                                ${open ? '<span class="badge badge-open">Open</span>' : ''}
                            </div>
                            <div class="network-meta">${getRSSI(net.rssi)}</div>
                        </div>
                        <div class="network-actions">
                            ${saved ? `<button class="btn-sm btn-forget" onclick="event.stopPropagation(); forget('${escapeHtml(net.ssid)}')">Forget</button>` : ''}
                        </div>
                    </div>`;
                });
            }

            // 2. Saved but not in range
            const notInRange = savedNetworks.filter(n => !inRange.has(n.ssid));
            if (notInRange.length > 0) {
                if (available.length > 0) {
                    html += '<div class="divider"></div>';
                }
                notInRange.forEach(net => {
                    html += `<div class="network-item not-in-range">
                        <div class="network-info">
                            <div class="network-name">
                                ${escapeHtml(net.ssid)}
                                <span class="badge badge-saved">Saved</span>
                            </div>
                            <div class="network-meta">Not in range • Priority: ${net.priority}</div>
                        </div>
                        <div class="network-actions">
                            <button class="btn-sm btn-forget" onclick="forget('${escapeHtml(net.ssid)}')">Forget</button>
                        </div>
                    </div>`;
                });
            }

            if (!html) {
                html = '<div class="empty-state">No networks found. Click ↻ to scan.</div>';
            }

            list.innerHTML = html;
        }

        function selectNetwork(ssid, isOpen) {
            document.getElementById('ssidInput').value = ssid;
            document.getElementById('ssidInput').readOnly = true;
            document.getElementById('passwordInput').value = '';
            document.getElementById('priorityInput').value = getSavedPriority(ssid);
            document.getElementById('addForm').classList.add('visible');
            if (!isOpen) {
                document.getElementById('passwordInput').focus();
            }
        }

        function showManualEntry() {
            document.getElementById('ssidInput').value = '';
            document.getElementById('ssidInput').readOnly = false;
            document.getElementById('passwordInput').value = '';
            document.getElementById('priorityInput').value = '50';
            document.getElementById('addForm').classList.add('visible');
            document.getElementById('ssidInput').focus();
        }

        function hideAddForm() {
            document.getElementById('addForm').classList.remove('visible');
        }

        async function saveAndConnect() {
            const ssid = document.getElementById('ssidInput').value;
            const password = document.getElementById('passwordInput').value;
            const priority = parseInt(document.getElementById('priorityInput').value) || 50;

            if (!ssid) {
                showMessage('Please enter a network name', true);
                return;
            }

            try {
                // Save the network
                const addRes = await fetch('/wifiman/add', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ ssid, password, priority })
                });

                if (!addRes.ok) {
                    showMessage('Failed to save network', true);
                    return;
                }

                showMessage('Network saved! Connecting...');
                hideAddForm();

                // Trigger connection
                await fetch('/wifiman/connect', { method: 'POST' });

                // Refresh after a delay
                setTimeout(refresh, 3000);
            } catch (e) {
                showMessage('Error: ' + e.message, true);
            }
        }

        async function forget(ssid) {
            if (!confirm(`Forget "${ssid}"?`)) return;

            try {
                const res = await fetch('/wifiman/remove', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ ssid })
                });

                if (res.ok) {
                    showMessage('Network forgotten');
                    await loadSaved();
                    renderNetworks();
                } else {
                    showMessage('Failed to remove network', true);
                }
            } catch (e) {
                showMessage('Error: ' + e.message, true);
            }
        }

        async function refresh() {
            const list = document.getElementById('networkList');
            list.innerHTML = '<div class="loading">Scanning...</div>';

            await Promise.all([loadStatus(), loadSaved()]);
            await scan();
        }

        // Initial load
        refresh();
        setInterval(loadStatus, 5000);
    </script>
</body>
</html>
)rawliteral";

} // namespace WiFiMan

#endif // WIFIMAN_WEBUI_H