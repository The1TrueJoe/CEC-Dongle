/* ── Globals shared across all modules ───────────────────────────────────── */
var $ = function (id) { return document.getElementById(id); };
var state = { status: null, logs: [], paused: false };

/* ── API helpers ─────────────────────────────────────────────────────────── */
async function api(url, method, body) {
  method = method || 'GET';
  try {
    var opts = { method: method, headers: { 'Content-Type': 'application/json' } };
    if (body) opts.body = JSON.stringify(body);
    var res  = await fetch(url, opts);
    var json = await res.json();
    if (json.success === false) toast(json.error || 'Request failed', 'err');
    else if (json.message) toast(json.message, 'ok');
    return json;
  } catch (e) { toast('Network error', 'err'); return null; }
}

async function apiCec(dst, data, src) {
  var body = { destination: dst, data: data };
  if (src !== undefined && src !== '' && src !== null) body.source = parseInt(src, 10);
  return api('/api/cec/send', 'POST', body);
}

/* ── Navigation ──────────────────────────────────────────────────────────── */
function activateView(name) {
  var btn = document.querySelector('.nav-btn[data-view="' + name + '"]');
  if (btn) btn.click();
}

function bindNav() {
  document.querySelectorAll('.nav-btn').forEach(function (btn) {
    btn.addEventListener('click', function () {
      document.querySelectorAll('.nav-btn').forEach(function (b) { b.classList.remove('active'); });
      document.querySelectorAll('.view').forEach(function (v) { v.classList.remove('active'); });
      btn.classList.add('active');
      $('view-' + btn.dataset.view).classList.add('active');
      if (btn.dataset.view === 'cec' || btn.dataset.view === 'network') loadConfig();
    });
  });
}

/* ── Status refresh ──────────────────────────────────────────────────────── */
async function refresh() {
  var s = await api('/api/status');
  if (!s) return;
  state.status = s;

  var dot = $('statusDot');
  dot.className = 'dot';
  if (s.wifi_status === 'Connected') dot.classList.add('on');
  else if (s.wifi_status === 'AP Mode') dot.classList.add('ap');

  $('statusText').textContent = s.wifi_status || '\u2014';
  $('statusIp').textContent   = s.wifi_ip     || '';
  $('statusUptime').textContent = fmtUptime(s.uptime_ms || 0);

  $('sysVersion').textContent  = s.version     || '\u2014';
  $('sysHostname').textContent = s.hostname    || '\u2014';
  $('sysIp').textContent       = s.wifi_ip     || '\u2014';
  $('sysWifi').textContent     = s.wifi_status || '\u2014';
  $('sysUptime').textContent   = fmtUptime(s.uptime_ms || 0);
  $('sysHeap').textContent     = (s.heap_free  || 0) + ' bytes';

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

/* ── Shared button bindings ──────────────────────────────────────────────── */
function bindButtons() {
  // data-api quick actions
  document.addEventListener('click', async function (e) {
    var t = e.target.closest('[data-api]');
    if (!t) return;
    await api(t.dataset.api, t.dataset.method || 'GET');
  });

  $('restartBtn').addEventListener('click', function () {
    if (confirm('Restart the device?')) api('/api/system/restart', 'POST');
  });
  $('resetBtn').addEventListener('click', function () {
    if (confirm('Factory reset will erase all settings. Continue?')) api('/api/system/reset', 'POST');
  });

  $('togglePause').addEventListener('click', function () {
    state.paused = !state.paused;
    $('togglePause').textContent = state.paused ? 'Resume' : 'Pause';
  });

  $('clearLog').addEventListener('click', async function () {
    await api('/api/cec/log', 'DELETE');
    state.logs = [];
    renderLog();
    renderRecentEvents();
  });

  $('exportLog').addEventListener('click', function () {
    if (!state.logs.length) { toast('No entries to export', 'err'); return; }
    var rows = state.logs.map(function (e) {
      return fmtMs(e.t) + '\t' + e.dir + '\t' + e.hex + '\t' + e.msg;
    });
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
  $('saveConfig').addEventListener('click',  saveConfig);
  $('reloadConfig').addEventListener('click', loadConfig);
}

/* ── Utilities ───────────────────────────────────────────────────────────── */
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
    .replace(/&/g, '&amp;').replace(/</g, '&lt;')
    .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}
function escAttr(s) { return esc(s); }
function toast(msg, type) {
  var t = document.createElement('div');
  t.className = 'toast ' + (type || 'ok');
  t.textContent = msg;
  $('toastWrap').appendChild(t);
  setTimeout(function () { t.remove(); }, 2800);
}

/* ── Bootstrap ───────────────────────────────────────────────────────────── */
function init() {
  bindNav();
  bindButtons();
  bindFrameBuilder();
  bindFilters();
  refresh();
  setInterval(refresh,     5000);
  setInterval(refreshLog,  1500);
}

init();
