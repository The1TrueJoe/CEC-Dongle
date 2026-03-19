(() => {
  const $ = (id) => document.getElementById(id);
  const $$ = (selector) => Array.from(document.querySelectorAll(selector));
  const LOGICAL_NAMES = ['TV', 'Rec1', 'Rec2', 'Tuner1', 'Play1', 'Audio', 'Tuner2', 'Tuner3', 'Play2', 'Rec3', 'Tuner4', 'Reserved', 'Reserved', 'Reserved', 'Free Use', 'Broadcast'];

  const state = {
    status: null,
    logs: [],
    paused: false,
    logTimer: null,
    statusTimer: null,
  };

  const VIEW_CONFIG_LOADERS = {
    settings: () => {
      loadConfig();
      renderSystemInfo();
    },
    diagnostics: () => refreshLog(),
  };

  function init() {
    bindNavigation();
    bindButtons();
    bindForms();
    bindFilters();
    refreshStatus();
    refreshLog();
    state.statusTimer = window.setInterval(refreshStatus, 5000);
    state.logTimer = window.setInterval(refreshLog, 1500);
  }

  async function api(url, method = 'GET', body = null) {
    try {
      const options = { method, headers: { 'Content-Type': 'application/json' } };
      if (body) {
        options.body = JSON.stringify(body);
      }

      const response = await fetch(url, options);
      const json = await response.json();

      if (json.success === false) {
        showToast(json.error || 'Request failed', 'error');
      } else if (json.message) {
        showToast(json.message, 'success');
      }

      return json;
    } catch (error) {
      showToast('Network error', 'error');
      return null;
    }
  }

  async function sendCec(destination, data, source) {
    const body = { destination, data };
    if (source !== undefined && source !== null && source !== '') {
      body.source = source;
    }
    return api('/api/cec/send', 'POST', body);
  }

  function bindNavigation() {
    $$('.nav-tab').forEach((button) => {
      button.addEventListener('click', () => {
        $$('.nav-tab').forEach((item) => item.classList.remove('active'));
        $$('.view').forEach((item) => item.classList.remove('active'));
        button.classList.add('active');
        $(`view-${button.dataset.view}`).classList.add('active');
        const loader = VIEW_CONFIG_LOADERS[button.dataset.view];
        if (loader) loader();
      });
    });
  }

  function bindButtons() {
    document.addEventListener('click', async (event) => {
      const target = event.target.closest('[data-api],[data-cec],[data-input]');
      if (!target) return;

      if (target.dataset.api) {
        await api(target.dataset.api, target.dataset.method || 'POST');
        return;
      }

      if (target.dataset.cec) {
        const payload = JSON.parse(target.dataset.cec);
        await sendCec(payload.destination, payload.data, payload.source);
        return;
      }

      if (target.dataset.input) {
        await setInput(parseInt(target.dataset.input, 16));
      }
    });

    $('sendOverviewPath').addEventListener('click', () => {
      const raw = $('overviewCustomPa').value.trim();
      if (!raw) return showToast('Enter a physical address', 'error');
      setInput(parseInt(raw, 16));
    });

    $('openDiagnostics').addEventListener('click', () => activateView('diagnostics'));
    $('togglePause').addEventListener('click', togglePause);
    $('clearLog').addEventListener('click', clearLog);
    $('exportLog').addEventListener('click', exportLog);
    $('scanDevices').addEventListener('click', scanDevices);
    $('saveConfig').addEventListener('click', saveConfig);
    $('reloadConfig').addEventListener('click', loadConfig);
    $('restartDevice').addEventListener('click', restartDevice);
    $('resetConfig').addEventListener('click', resetConfig);
    $('scanWifi').addEventListener('click', scanWifi);
    $('connectWifi').addEventListener('click', connectWifi);
    $('sendBuiltCommand').addEventListener('click', sendBuiltCommand);
    $('fillActiveSource').addEventListener('click', fillActiveSourcePayload);
    $('sendRawHex').addEventListener('click', sendRawHex);
  }

  function bindForms() {
    ['builderSource', 'builderDestination', 'builderOpcode', 'builderOperands'].forEach((id) => {
      $(id).addEventListener('input', updateBuilderPreview);
      $(id).addEventListener('change', updateBuilderPreview);
    });
  }

  function bindFilters() {
    ['filterDirection', 'filterSource', 'filterDestination', 'filterOpcode', 'filterText'].forEach((id) => {
      $(id).addEventListener('input', renderLog);
    });
  }

  function activateView(viewName) {
    const button = document.querySelector(`.nav-tab[data-view="${viewName}"]`);
    if (button) button.click();
  }

  async function refreshStatus() {
    const status = await api('/api/status');
    if (!status) return;

    state.status = status;
    updateHeader(status);
    updateHero(status);
    updateStats(status);
    renderSystemInfo();
  }

  function updateHeader(status) {
    const dot = $('headerStatusDot');
    dot.className = 'status-dot';
    if (status.wifi_status === 'Connected') {
      dot.classList.add('online');
    } else if (status.wifi_status === 'AP Mode') {
      dot.classList.add('warning');
    }

    $('headerStatusText').textContent = status.wifi_status;
    $('headerAddress').textContent = status.wifi_ip || '--';
    $('headerVersion').textContent = status.version || '--';
  }

  function updateHero(status) {
    const connected = status.wifi_status === 'Connected';
    $('heroTitle').textContent = connected ? 'Gateway is online and ready for automation' : 'Gateway is available for provisioning and diagnostics';
    $('heroCopy').textContent = connected
      ? `Connected at ${status.wifi_ip}. Use the actions below to control sources, audio, and CEC workflows.`
      : 'The device is not on infrastructure WiFi yet. Use the WiFi page to provision it or continue using AP mode.';
  }

  function updateStats(status) {
    $('statNetwork').textContent = status.wifi_status;
    $('statIp').textContent = status.wifi_ip || 'No IP address';
    $('statIdentity').textContent = `${toHex(status.cec_address, 2)} · ${status.cec_osd_name || 'Unnamed'}`;
    $('statPhysical').textContent = `Physical address ${toHex(status.cec_physical, 4)}`;
    $('statUptime').textContent = formatUptime(status.uptime_ms || 0);
    $('statHeap').textContent = `${status.heap_free} bytes free`;
    $('statUiMode').textContent = status.ui_storage === 'littlefs-gzip' ? 'LittleFS + gzip' : 'Embedded';
    $('statOta').textContent = status.ota_enabled ? 'Arduino OTA enabled' : 'OTA unavailable';
  }

  async function refreshLog() {
    if (state.paused) return;
    const entries = await api('/api/cec/log');
    if (!entries || !Array.isArray(entries)) return;
    state.logs = entries;
    renderLog();
    renderOverviewEvents();
  }

  function renderLog() {
    const direction = $('filterDirection').value;
    const source = $('filterSource').value.trim().toLowerCase();
    const destination = $('filterDestination').value.trim().toLowerCase();
    const opcode = $('filterOpcode').value.trim().toLowerCase();
    const text = $('filterText').value.trim().toLowerCase();

    const filtered = state.logs.filter((entry) => {
      if (direction && entry.dir !== direction) return false;
      const parts = entry.hex.split(':');
      const header = parts[0] ? parseInt(parts[0], 16) : NaN;
      const src = Number.isNaN(header) ? '' : ((header >> 4) & 0xF).toString(16);
      const dst = Number.isNaN(header) ? '' : (header & 0xF).toString(16);
      const op = parts[1] ? parts[1].toLowerCase() : '';
      if (source && src !== source) return false;
      if (destination && dst !== destination) return false;
      if (opcode && op !== opcode) return false;
      if (text && !entry.hex.toLowerCase().includes(text) && !entry.msg.toLowerCase().includes(text)) return false;
      return true;
    });

    $('logCount').textContent = String(state.logs.length);
    $('rxCount').textContent = String(state.logs.filter((entry) => entry.dir === 'rx').length);
    $('txCount').textContent = String(state.logs.filter((entry) => entry.dir === 'tx').length);

    if (!filtered.length) {
      $('logList').innerHTML = '<div class="empty-state">No entries match the current filters.</div>';
      return;
    }

    $('logList').innerHTML = filtered.map((entry) => {
      return `
        <article class="log-entry">
          <span class="log-time">${formatLogTime(entry.t)}</span>
          <span class="log-dir ${entry.dir}">${entry.dir.toUpperCase()}</span>
          <code class="log-hex">${escapeHtml(entry.hex)}</code>
          <span class="log-readable">${escapeHtml(entry.msg)}</span>
          <button class="copy-btn" data-copy="${escapeAttribute(entry.hex)}">Copy</button>
        </article>
      `;
    }).join('');

    $$('.copy-btn').forEach((button) => {
      button.addEventListener('click', async () => {
        await navigator.clipboard.writeText(button.dataset.copy);
        showToast('Copied frame to clipboard', 'success');
      });
    });

    if ($('autoScroll').checked) {
      $('logList').scrollTop = $('logList').scrollHeight;
    }
  }

  function renderOverviewEvents() {
    const latest = [...state.logs].slice(-6).reverse();
    if (!latest.length) {
      $('overviewEvents').innerHTML = '<div class="empty-state">No events yet.</div>';
      return;
    }

    $('overviewEvents').innerHTML = latest.map((entry) => `
      <article class="event-item">
        <strong>${entry.dir.toUpperCase()} · ${escapeHtml(entry.hex)}</strong>
        <span class="event-meta">${formatLogTime(entry.t)} · ${escapeHtml(entry.msg)}</span>
      </article>
    `).join('');
  }

  function togglePause() {
    state.paused = !state.paused;
    $('togglePause').textContent = state.paused ? 'Resume' : 'Pause';
  }

  async function clearLog() {
    await api('/api/cec/log', 'DELETE');
    state.logs = [];
    renderLog();
    renderOverviewEvents();
  }

  function exportLog() {
    if (!state.logs.length) {
      showToast('No log entries to export', 'error');
      return;
    }

    const rows = state.logs.map((entry) => `${formatLogTime(entry.t)}\t${entry.dir}\t${entry.hex}\t${entry.msg}`);
    const blob = new Blob(['time\tdirection\thex\tmessage\n' + rows.join('\n')], { type: 'text/tab-separated-values' });
    const link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = `cec-log-${Date.now()}.tsv`;
    link.click();
  }

  async function setInput(physicalAddress) {
    if (!physicalAddress) {
      showToast('Invalid physical address', 'error');
      return;
    }
    await api('/api/cec/input', 'POST', { physical_address: physicalAddress });
  }

  function updateBuilderPreview() {
    const source = $('builderSource').value;
    const destination = $('builderDestination').value || '0';
    const opcode = $('builderOpcode').value;
    const operands = $('builderOperands').value.trim();

    if (!opcode) {
      $('builderPreview').textContent = '--';
      return;
    }

    const sourceHex = source === '' ? '*' : Number(source).toString(16).toUpperCase();
    const destinationHex = Number(destination).toString(16).toUpperCase();
    const renderedOperands = operands
      ? operands.split(/[\s,:]+/).filter(Boolean).map((value) => value.toUpperCase()).join(':')
      : '';

    $('builderPreview').textContent = `${sourceHex}${destinationHex}:${opcode}${renderedOperands ? ':' + renderedOperands : ''}`;
  }

  async function sendBuiltCommand() {
    const opcode = $('builderOpcode').value;
    if (!opcode) {
      showToast('Choose an opcode first', 'error');
      return;
    }

    const bytes = [parseInt(opcode, 16)];
    const operands = $('builderOperands').value.trim();
    if (operands) {
      operands.split(/[\s,:]+/).filter(Boolean).forEach((value) => bytes.push(parseInt(value, 16)));
    }

    const source = $('builderSource').value;
    const destination = parseInt($('builderDestination').value, 10);
    await sendCec(destination, bytes, source === '' ? undefined : parseInt(source, 10));
  }

  function fillActiveSourcePayload() {
    const physicalAddress = state.status?.cec_physical || 0x1000;
    $('builderDestination').value = '15';
    $('builderOpcode').value = '82';
    $('builderOperands').value = `${toByte(physicalAddress >> 8)} ${toByte(physicalAddress & 0xFF)}`;
    updateBuilderPreview();
  }

  async function sendRawHex() {
    const raw = $('rawHex').value.trim();
    if (!raw) {
      showToast('Enter one or more hex bytes', 'error');
      return;
    }

    const bytes = raw.split(/[\s,:]+/).filter(Boolean).map((value) => parseInt(value, 16));
    await sendCec(parseInt($('rawDestination').value, 10), bytes);
  }

  async function scanDevices() {
    $('deviceGrid').innerHTML = '<div class="empty-state">Scanning 15 logical addresses...</div>';

    for (let address = 0; address < 15; address += 1) {
      await sendCec(address, [0x46]);
      await sendCec(address, [0x8F]);
    }

    showToast('Scan sent. Waiting for responses...', 'success');

    window.setTimeout(async () => {
      await refreshLog();
      renderDevicesFromLog();
    }, 4000);
  }

  function renderDevicesFromLog() {
    const discovered = new Map();

    state.logs.forEach((entry) => {
      if (entry.dir !== 'rx') return;
      const parts = entry.hex.split(':');
      if (parts.length < 2) return;

      const header = parseInt(parts[0], 16);
      const source = (header >> 4) & 0xF;
      const opcode = parseInt(parts[1], 16);

      if (!discovered.has(source)) {
        discovered.set(source, {
          address: source,
          logicalName: LOGICAL_NAMES[source],
          osd: '',
          power: 'Unknown',
        });
      }

      const record = discovered.get(source);

      if (opcode === 0x47) {
        record.osd = parts.slice(2).map((part) => String.fromCharCode(parseInt(part, 16))).join('');
      }

      if (opcode === 0x90 && parts[2]) {
        record.power = ['On', 'Standby', 'Transitioning On', 'Transitioning Standby'][parseInt(parts[2], 16)] || 'Unknown';
      }
    });

    if (!discovered.size) {
      $('deviceGrid').innerHTML = '<div class="empty-state">No devices responded. Enable promiscuous mode to capture more traffic.</div>';
      return;
    }

    $('deviceGrid').innerHTML = [...discovered.values()]
      .sort((a, b) => a.address - b.address)
      .map((device) => `
        <article class="device-item">
          <span class="device-title">${toHex(device.address, 1)} · ${device.logicalName}</span>
          <span>${escapeHtml(device.osd || '(no OSD name reported)')}</span>
          <span class="wifi-meta">Power state: ${escapeHtml(device.power)}</span>
        </article>
      `)
      .join('');
  }

  async function loadConfig() {
    const config = await api('/api/config');
    if (!config) return;

    $('cfgPin').value = config.cec_pin;
    $('cfgAddress').value = toHex(config.cec_address, 2);
    $('cfgPhysical').value = toHex(config.cec_physical, 4);
    $('cfgOsdName').value = config.cec_osd_name;
    $('cfgHostname').value = config.hostname;
    $('cfgLogBuffer').value = config.log_buffer_size;
    $('cfgPromiscuous').checked = Boolean(config.cec_promiscuous);
    $('cfgMonitorMode').checked = Boolean(config.cec_monitor_mode);
  }

  async function saveConfig() {
    await api('/api/config', 'POST', {
      cec_pin: parseInt($('cfgPin').value, 10),
      cec_address: parseInt($('cfgAddress').value, 16),
      cec_physical: parseInt($('cfgPhysical').value, 16),
      cec_osd_name: $('cfgOsdName').value,
      hostname: $('cfgHostname').value,
      log_buffer_size: parseInt($('cfgLogBuffer').value, 10),
      cec_promiscuous: $('cfgPromiscuous').checked,
      cec_monitor_mode: $('cfgMonitorMode').checked,
    });
  }

  function renderSystemInfo() {
    const status = state.status;
    if (!status) return;

    const rows = [
      ['Version', status.version || '--'],
      ['Hostname', status.hostname || '--'],
      ['WiFi', status.wifi_status || '--'],
      ['IP Address', status.wifi_ip || '--'],
      ['Heap', `${status.heap_free} bytes`],
      ['CEC Address', toHex(status.cec_address, 2)],
      ['CEC Physical', toHex(status.cec_physical, 4)],
      ['OTA', status.ota_enabled ? 'Enabled' : 'Disabled'],
    ];

    $('systemInfoList').innerHTML = rows.map(([label, value]) => `
      <div>
        <dt>${label}</dt>
        <dd>${escapeHtml(String(value))}</dd>
      </div>
    `).join('');
  }

  function restartDevice() {
    if (window.confirm('Restart the device now?')) {
      api('/api/system/restart', 'POST');
    }
  }

  function resetConfig() {
    if (window.confirm('Factory reset will erase configuration and restart the device. Continue?')) {
      api('/api/system/reset', 'POST');
    }
  }

  async function scanWifi() {
    $('wifiList').innerHTML = '<div class="empty-state">Scanning for nearby networks...</div>';
    const networks = await api('/api/wifi/scan');
    if (!networks || !Array.isArray(networks) || !networks.length) {
      $('wifiList').innerHTML = '<div class="empty-state">No WiFi networks found.</div>';
      return;
    }

    $('wifiList').innerHTML = networks
      .sort((left, right) => right.rssi - left.rssi)
      .map((network) => `
        <button class="wifi-item" data-ssid="${escapeAttribute(network.ssid)}">
          <span>
            <strong>${escapeHtml(network.ssid)}</strong>
            <div class="wifi-meta">${network.encrypted ? 'Secured' : 'Open'} network</div>
          </span>
          <span class="wifi-meta">${network.rssi} dBm · ${signalBars(network.rssi)}</span>
        </button>
      `)
      .join('');

    $$('.wifi-item').forEach((button) => {
      button.addEventListener('click', () => {
        $('wifiSsid').value = button.dataset.ssid;
      });
    });
  }

  async function connectWifi() {
    const ssid = $('wifiSsid').value.trim();
    const password = $('wifiPassword').value;
    if (!ssid) {
      showToast('Enter an SSID', 'error');
      return;
    }

    await api('/api/wifi/connect', 'POST', { ssid, password });
  }

  function signalBars(rssi) {
    if (rssi >= -50) return '▉▉▉▉';
    if (rssi >= -65) return '▉▉▉';
    if (rssi >= -75) return '▉▉';
    return '▉';
  }

  function formatUptime(milliseconds) {
    const totalSeconds = Math.floor(milliseconds / 1000);
    const hours = Math.floor(totalSeconds / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    return hours > 0 ? `${hours}h ${minutes}m` : `${minutes}m`;
  }

  function formatLogTime(milliseconds) {
    return `${(milliseconds / 1000).toFixed(1)}s`;
  }

  function toHex(value, width) {
    return `0x${Number(value ?? 0).toString(16).toUpperCase().padStart(width, '0')}`;
  }

  function toByte(value) {
    return Number(value).toString(16).toUpperCase().padStart(2, '0');
  }

  function escapeHtml(value) {
    return value
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  function escapeAttribute(value) {
    return escapeHtml(value);
  }

  function showToast(message, tone) {
    const toast = document.createElement('div');
    toast.className = `toast ${tone === 'error' ? 'error' : 'success'}`;
    toast.textContent = message;
    document.body.appendChild(toast);
    window.setTimeout(() => toast.remove(), 2600);
  }

  init();
})();
