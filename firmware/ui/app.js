(() => {
  const $ = id => document.getElementById(id);

  const state = { status: null, logs: [], paused: false };

  /* ── Bootstrap ─────────────────────────────────────────────────────── */
  function init() {
    bindNav();
    bindButtons();
    bindFrameBuilder();
    bindFilters();
    refresh();
    setInterval(refresh, 5000);
    setInterval(refreshLog, 1500);
  }

  /* ── Navigation ─────────────────────────────────────────────────────── */
  function bindNav() {
    document.querySelectorAll('.nav-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));
        document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
        btn.classList.add('active');
        $('view-' + btn.dataset.view).classList.add('active');
        if (btn.dataset.view === 'cec' || btn.dataset.view === 'network') loadConfig();
      });
    });
  }

  function activateView(name) {
    const btn = document.querySelector('.nav-btn[data-view="' + name + '"]');
    if (btn) btn.click();
  }

  /* ── API helpers ────────────────────────────────────────────────────── */
  async function api(url, method, body) {
    method = method || 'GET';
    try {
      const opts = { method, headers: { 'Content-Type': 'application/json' } };
      if (body) opts.body = JSON.stringify(body);
      const res = await fetch(url, opts);
      const json = await res.json();
      if (json.success === false) toast(json.error || 'Request failed', 'err');
      else if (json.message) toast(json.message, 'ok');
      return json;
    } catch (e) { toast('Network error', 'err'); return null; }
  }

  async function apiCec(dst, data, src) {
    const body = { destination: dst, data: data };
    if (src !== undefined && src !== '' && src !== null) body.source = parseInt(src, 10);
    return api('/api/cec/send', 'POST', body);
  }

  /* ── Status refresh ─────────────────────────────────────────────────── */
  async function refresh() {
    const s = await api('/api/status');
    if (!s) return;
    state.status = s;

    const dot = $('statusDot');
    dot.className = 'dot';
    if (s.wifi_status === 'Connected') dot.classList.add('on');
    else if (s.wifi_status === 'AP Mode') dot.classList.add('ap');

    $('statusText').textContent = s.wifi_status || '\u2014';
    $('statusIp').textContent   = s.wifi_ip    || '';
    $('statusUptime').textContent = fmtUptime(s.uptime_ms || 0);

    $('sysVersion').textContent  = s.version    || '\u2014';
    $('sysHostname').textContent = s.hostname   || '\u2014';
    $('sysIp').textContent       = s.wifi_ip    || '\u2014';
    $('sysWifi').textContent     = s.wifi_status|| '\u2014';
    $('sysUptime').textContent   = fmtUptime(s.uptime_ms || 0);
    $('sysHeap').textContent     = (s.heap_free || 0) + ' bytes';

    $('cecAddr').textContent      = hex(s.cec_address, 2);
    $('cecPhys').textContent      = hex(s.cec_physical, 4);
    $('cecOsd').textContent       = s.cec_osd_name || '\u2014';
    $('cecTv').textContent        = hex(s.tv_logical_address    != null ? s.tv_logical_address    : 0, 1);
    $('cecAudio').textContent     = hex(s.audio_logical_address != null ? s.audio_logical_address : 5, 1);
    $('cecVolTarget').textContent = s.volume_target || '\u2014';

    $('netStatus').textContent   = s.wifi_status || '\u2014';
    $('netSsid').textContent     = s.wifi_ssid   || '\u2014';
    $('netIp').textContent       = s.wifi_ip     || '\u2014';
    $('netHostname').textContent = s.hostname    || '\u2014';

    $('fwVersion').textContent = s.version || '\u2014';
  }

  /* ── CEC log refresh ────────────────────────────────────────────────── */
  async function refreshLog() {
    if (state.paused) return;
    const entries = await api('/api/cec/log');
    if (!entries || !Array.isArray(entries)) return;
    state.logs = entries;
    renderLog();
    renderRecentEvents();
  }

  function renderLog() {
    const dir = $('filterDir').value;
    const src = $('filterSrc').value.trim().toLowerCase();
    const dst = $('filterDst').value.trim().toLowerCase();
    const op  = $('filterOp').value.trim().toLowerCase();
    const txt = $('filterTxt').value.trim().toLowerCase();

    const list = state.logs.filter(function(e) {
      if (dir && e.dir !== dir) return false;
      const parts  = e.hex.split(':');
      const header = parseInt(parts[0], 16);
      const esrc   = ((header >> 4) & 0xF).toString(16);
      const edst   = (header & 0xF).toString(16);
      const eop    = parts[1] ? parts[1].toLowerCase() : '';
      if (src && esrc !== src) return false;
      if (dst && edst !== dst) return false;
      if (op  && eop  !== op)  return false;
      if (txt && !e.hex.toLowerCase().includes(txt) && !e.msg.toLowerCase().includes(txt)) return false;
      return true;
    });

    $('logTotal').textContent = state.logs.length;
    $('logRx').textContent    = state.logs.filter(function(e){ return e.dir === 'rx'; }).length;
    $('logTx').textContent    = state.logs.filter(function(e){ return e.dir === 'tx'; }).length;

    if (!list.length) {
      $('logList').innerHTML = '<div class="empty">No entries match filters.</div>';
      return;
    }

    $('logList').innerHTML = list.map(function(e) {
      return '<div class="log-entry">' +
        '<span class="log-t">' + fmtMs(e.t) + '</span>' +
        '<span class="log-dir ' + e.dir + '">' + e.dir.toUpperCase() + '</span>' +
        '<code class="log-hex">' + esc(e.hex) + '</code>' +
        '<span class="log-msg">' + esc(e.msg) + '</span>' +
        '<button class="log-copy" data-h="' + escAttr(e.hex) + '">\u2398</button>' +
        '</div>';
    }).join('');

    $('logList').querySelectorAll('.log-copy').forEach(function(btn) {
      btn.addEventListener('click', function() {
        navigator.clipboard.writeText(btn.dataset.h);
        toast('Copied', 'ok');
      });
    });

    if ($('autoScroll').checked) $('logList').scrollTop = $('logList').scrollHeight;
  }

  function renderRecentEvents() {
    const latest = state.logs.slice(-8).reverse();
    if (!latest.length) {
      $('recentEvents').innerHTML = '<div class="empty">No events yet.</div>';
      return;
    }
    $('recentEvents').innerHTML = '<div class="log-list" style="max-height:180px">' +
      latest.map(function(e) {
        return '<div class="log-entry">' +
          '<span class="log-t">' + fmtMs(e.t) + '</span>' +
          '<span class="log-dir ' + e.dir + '">' + e.dir.toUpperCase() + '</span>' +
          '<code class="log-hex">' + esc(e.hex) + '</code>' +
          '<span class="log-msg">' + esc(e.msg) + '</span>' +
          '<span></span></div>';
      }).join('') + '</div>';
  }

  /* ── Config load / save ─────────────────────────────────────────────── */
  async function loadConfig() {
    const c = await api('/api/config');
    if (!c) return;
    $('cfgPin').value           = c.cec_pin          != null ? c.cec_pin          : 14;
    $('cfgAddress').value       = c.cec_address       != null ? c.cec_address       : 4;
    $('cfgPhysical').value      = ((c.cec_physical != null ? c.cec_physical : 0x4000)).toString(16).toUpperCase();
    $('cfgOsdName').value       = c.cec_osd_name      || 'CEC-Dongle';
    $('cfgHostname').value      = c.hostname          || 'cec-dongle';
    $('cfgLogBuffer').value     = c.log_buffer_size   != null ? c.log_buffer_size   : 50;
    $('cfgPromiscuous').checked = Boolean(c.cec_promiscuous);
    $('cfgMonitorMode').checked = Boolean(c.cec_monitor_mode);
    $('cfgDeviceType').value    = c.device_type       || 'playback';
    $('cfgAutoNeg').checked     = Boolean(c.auto_negotiate);
    $('cfgTvAddress').value     = c.tv_logical_address    != null ? c.tv_logical_address    : 0;
    $('cfgAudioAddress').value  = c.audio_logical_address != null ? c.audio_logical_address : 5;
    $('cfgVolumeTarget').value  = c.volume_target     || 'audio';
    $('cfgPowerOnCmd').value    = c.power_on_command  || 'image_view_on';
  }

  async function saveConfig() {
    await api('/api/config', 'POST', {
      cec_pin:               parseInt($('cfgPin').value, 10),
      cec_address:           parseInt($('cfgAddress').value, 10),
      cec_physical:          parseInt($('cfgPhysical').value, 16),
      cec_osd_name:          $('cfgOsdName').value.trim(),
      hostname:              $('cfgHostname').value.trim(),
      log_buffer_size:       parseInt($('cfgLogBuffer').value, 10),
      cec_promiscuous:       $('cfgPromiscuous').checked,
      cec_monitor_mode:      $('cfgMonitorMode').checked,
      device_type:           $('cfgDeviceType').value,
      auto_negotiate:        $('cfgAutoNeg').checked,
      tv_logical_address:    parseInt($('cfgTvAddress').value, 10),
      audio_logical_address: parseInt($('cfgAudioAddress').value, 10),
      volume_target:         $('cfgVolumeTarget').value,
      power_on_command:      $('cfgPowerOnCmd').value,
    });
  }

  /* ── WiFi scan / connect ────────────────────────────────────────────── */
  async function scanWifi() {
    const card = $('wifiScanCard');
    card.style.display = 'block';
    $('wifiScanResult').innerHTML = '<div class="empty">Scanning\u2026</div>';
    const nets = await api('/api/wifi/scan');
    if (!nets || !Array.isArray(nets) || !nets.length) {
      $('wifiScanResult').innerHTML = '<div class="empty">No networks found.</div>';
      return;
    }
    const sorted = nets.slice().sort(function(a, b) { return b.rssi - a.rssi; });
    $('wifiScanResult').innerHTML = '<table class="wifi-list">' +
      sorted.map(function(n) {
        return '<tr data-ssid="' + escAttr(n.ssid) + '">' +
          '<td><span class="wifi-ssid">' + esc(n.ssid) + '</span><br>' +
          '<span class="wifi-meta">' + (n.encrypted ? 'Secured' : 'Open') + '</span></td>' +
          '<td class="wifi-meta" style="text-align:right">' + n.rssi + ' dBm</td></tr>';
      }).join('') + '</table>';
    $('wifiScanResult').querySelectorAll('tr').forEach(function(row) {
      row.addEventListener('click', function() { $('wifiSsid').value = row.dataset.ssid; });
    });
  }

  async function connectWifi() {
    const ssid = $('wifiSsid').value.trim();
    const pass = $('wifiPassword').value;
    if (!ssid) { toast('Enter an SSID', 'err'); return; }
    await api('/api/wifi/connect', 'POST', { ssid: ssid, password: pass });
  }

  /* ── OTA upload ─────────────────────────────────────────────────────── */
  function uploadFirmware() {
    const file = $('otaFile').files[0];
    if (!file) { toast('Select a .bin file', 'err'); return; }
    if (!file.name.endsWith('.bin')) { toast('File must be .bin', 'err'); return; }
    const prog = $('otaProgress'), bar = $('otaBar'), lbl = $('otaLabel'), btn = $('uploadBtn');
    prog.style.display = 'block';
    bar.style.width = '0%';
    lbl.textContent = '0%';
    btn.disabled = true;
    const form = new FormData();
    form.append('firmware', file);
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/ota/update');
    xhr.upload.onprogress = function(e) {
      if (!e.lengthComputable) return;
      const p = Math.round(e.loaded / e.total * 100);
      bar.style.width = p + '%';
      lbl.textContent = p + '%';
    };
    xhr.onload = function() {
      if (xhr.status === 200) {
        bar.style.width = '100%';
        lbl.textContent = '100% \u2014 restarting\u2026';
        toast('Firmware uploaded \u2014 restarting', 'ok');
      } else {
        var msg = 'Upload failed';
        try { msg = JSON.parse(xhr.responseText).error || msg; } catch (e2) {}
        toast(msg, 'err');
        btn.disabled = false;
        prog.style.display = 'none';
      }
    };
    xhr.onerror = function() {
      toast('Network error during upload', 'err');
      btn.disabled = false;
      prog.style.display = 'none';
    };
    xhr.send(form);
  }

  /* ── Frame builder ──────────────────────────────────────────────────── */
  function bindFrameBuilder() {
    ['txSrc', 'txDst', 'txOpcode', 'txOperands'].forEach(function(id) {
      $(id).addEventListener('input',  updatePreview);
      $(id).addEventListener('change', updatePreview);
    });
  }

  function updatePreview() {
    var src = $('txSrc').value;
    var dst = $('txDst').value || '0';
    var op  = $('txOpcode').value;
    if (!op) { $('txPreview').textContent = '\u2014'; return; }
    var sHex = src === '' ? '*' : Number(src).toString(16).toUpperCase();
    var dHex = Number(dst).toString(16).toUpperCase();
    var ops  = $('txOperands').value.trim();
    var extra = ops
      ? ':' + ops.split(/[\s,:]+/).filter(Boolean).map(function(v){ return v.toUpperCase(); }).join(':')
      : '';
    $('txPreview').textContent = sHex + dHex + ':' + op.toUpperCase() + extra;
  }

  async function sendFrameCmd() {
    var op = $('txOpcode').value;
    if (!op) { toast('Select an opcode', 'err'); return; }
    var bytes = [parseInt(op, 16)];
    var ops = $('txOperands').value.trim();
    if (ops) ops.split(/[\s,:]+/).filter(Boolean).forEach(function(v){ bytes.push(parseInt(v, 16)); });
    var src = $('txSrc').value;
    var dst = parseInt($('txDst').value, 10);
    await apiCec(dst, bytes, src === '' ? undefined : src);
    await refreshLog();
  }

  /* ── Button bindings ────────────────────────────────────────────────── */
  function bindButtons() {
    // data-api quick actions
    document.addEventListener('click', async function(e) {
      var t = e.target.closest('[data-api]');
      if (!t) return;
      await api(t.dataset.api, t.dataset.method || 'GET');
    });

    $('saveConfig').addEventListener('click',  saveConfig);
    $('reloadConfig').addEventListener('click', loadConfig);

    $('togglePause').addEventListener('click', function() {
      state.paused = !state.paused;
      $('togglePause').textContent = state.paused ? 'Resume' : 'Pause';
    });

    $('clearLog').addEventListener('click', async function() {
      await api('/api/cec/log', 'DELETE');
      state.logs = [];
      renderLog();
      renderRecentEvents();
    });

    $('exportLog').addEventListener('click', function() {
      if (!state.logs.length) { toast('No entries to export', 'err'); return; }
      var rows = state.logs.map(function(e){ return fmtMs(e.t) + '\t' + e.dir + '\t' + e.hex + '\t' + e.msg; });
      var blob = new Blob(['time\tdir\thex\tmsg\n' + rows.join('\n')], { type: 'text/tab-separated-values' });
      var a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = 'cec-log-' + Date.now() + '.tsv';
      a.click();
    });

    $('sendFrameBtn').addEventListener('click', sendFrameCmd);
    $('scanWifi').addEventListener('click',    scanWifi);
    $('connectWifi').addEventListener('click', connectWifi);
    $('uploadBtn').addEventListener('click',   uploadFirmware);

    $('restartBtn').addEventListener('click', function() {
      if (confirm('Restart the device?')) api('/api/system/restart', 'POST');
    });
    $('resetBtn').addEventListener('click', function() {
      if (confirm('Factory reset will erase all settings. Continue?')) api('/api/system/reset', 'POST');
    });
  }

  function bindFilters() {
    ['filterDir', 'filterSrc', 'filterDst', 'filterOp', 'filterTxt'].forEach(function(id) {
      $(id).addEventListener('input', renderLog);
    });
  }

  /* ── Utilities ──────────────────────────────────────────────────────── */
  function hex(v, w) {
    return '0x' + Number(v != null ? v : 0).toString(16).toUpperCase().padStart(w, '0');
  }
  function fmtMs(ms) { return (ms / 1000).toFixed(1) + 's'; }
  function fmtUptime(ms) {
    var s = Math.floor(ms / 1000), h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60);
    return h > 0 ? h + 'h ' + m + 'm' : m + 'm';
  }
  function esc(s) {
    return String(s)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
  function escAttr(s) { return esc(s); }
  function toast(msg, type) {
    var t = document.createElement('div');
    t.className = 'toast ' + type;
    t.textContent = msg;
    $('toastWrap').appendChild(t);
    setTimeout(function(){ t.remove(); }, 2800);
  }

  init();
})();
