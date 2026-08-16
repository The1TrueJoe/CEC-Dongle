/* ── Config load / save ──────────────────────────────────────────────────── */

async function loadConfig() {
  var c = await api('/api/config');
  if (!c) return;
  $('cfgPin').value           = c.cec_pin          != null ? c.cec_pin          : 14;
  $('cfgAddress').value       = c.cec_address       != null ? c.cec_address       : 4;
  $('cfgPhysical').value      = (c.cec_physical     != null ? c.cec_physical      : 0x4000).toString(16).toUpperCase();
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
  $('cfgPowerOffCmd').value   = c.power_off_command || 'standby';
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
    power_off_command:     $('cfgPowerOffCmd').value,
  });
}

/* ── WiFi scan / connect ─────────────────────────────────────────────────── */

async function scanWifi() {
  var card = $('wifiScanCard');
  card.style.display = 'block';
  $('wifiScanResult').innerHTML = '<div class="empty">Scanning\u2026</div>';
  // Poll until the async scan completes (firmware returns {scanning:true} while running)
  var data;
  for (;;) {
    data = await api('/api/wifi/scan');
    if (!data || !data.scanning) break;
    await new Promise(function (r) { setTimeout(r, 1000); });
  }
  var nets = data && data.networks;
  if (!nets || !nets.length) {
    $('wifiScanResult').innerHTML = '<div class="empty">No networks found.</div>';
    return;
  }
  var sorted = nets.slice().sort(function (a, b) { return b.rssi - a.rssi; });
  $('wifiScanResult').innerHTML = '<table class="wifi-list">' +
    sorted.map(function (n) {
      return '<tr data-ssid="' + escAttr(n.ssid) + '">' +
        '<td><span class="wifi-ssid">' + esc(n.ssid) + '</span><br>' +
        '<span class="wifi-meta">' + (n.encrypted ? 'Secured' : 'Open') + '</span></td>' +
        '<td class="wifi-meta" style="text-align:right">' + n.rssi + ' dBm</td></tr>';
    }).join('') + '</table>';
  $('wifiScanResult').querySelectorAll('tr').forEach(function (row) {
    row.addEventListener('click', function () { $('wifiSsid').value = row.dataset.ssid; });
  });
}

async function connectWifi() {
  var ssid = $('wifiSsid').value.trim();
  var pass = $('wifiPassword').value;
  if (!ssid) { toast('Enter an SSID', 'err'); return; }
  await api('/api/wifi/connect', 'POST', { ssid: ssid, password: pass });
}

/* ── OTA upload ──────────────────────────────────────────────────────────── */

function uploadFirmware() {
  var file = $('otaFile').files[0];
  if (!file) { toast('Select a .bin file', 'err'); return; }
  if (!file.name.endsWith('.bin')) { toast('File must be .bin', 'err'); return; }
  var prog = $('otaProgress'), bar = $('otaBar'), lbl = $('otaLabel'), btn = $('uploadBtn');
  prog.style.display = 'block';
  bar.style.width = '0%';
  lbl.textContent = '0%';
  btn.disabled = true;
  var form = new FormData();
  form.append('firmware', file);
  var xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/ota/update');
  xhr.upload.onprogress = function (e) {
    if (!e.lengthComputable) return;
    var p = Math.round(e.loaded / e.total * 100);
    bar.style.width = p + '%';
    lbl.textContent = p + '%';
  };
  xhr.onload = function () {
    if (xhr.status === 200) {
      bar.style.width = '100%';
      lbl.textContent = '100% \u2014 restarting\u2026';
      var msg = 'Uploaded \u2014 restarting';
      try { msg = JSON.parse(xhr.responseText).message || msg; } catch (e2) {}
      toast(msg, 'ok');
    } else {
      var msg = 'Upload failed';
      try { msg = JSON.parse(xhr.responseText).error || msg; } catch (e2) {}
      toast(msg, 'err');
      btn.disabled = false;
      prog.style.display = 'none';
    }
  };
  xhr.onerror = function () {
    toast('Network error during upload', 'err');
    btn.disabled = false;
    prog.style.display = 'none';
  };
  xhr.send(form);
}
