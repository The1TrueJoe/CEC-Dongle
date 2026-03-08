/*
 * Embedded Web UI — single-page HTML served from PROGMEM
 * Includes WiFi setup, CEC config, live log, and quick-action buttons
 */

#pragma once

#include <Arduino.h>

static const char WEB_UI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CEC Dongle</title>
<style>
  :root { --bg:#1a1a2e; --card:#16213e; --accent:#0f3460; --hl:#e94560; --text:#eee; --dim:#888; --ok:#27ae60; --err:#c0392b; }
  * { box-sizing:border-box; margin:0; padding:0; }
  body { font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif; background:var(--bg); color:var(--text); padding:12px; max-width:800px; margin:auto; }
  h1 { font-size:1.4em; margin-bottom:4px; }
  h2 { font-size:1.1em; color:var(--hl); margin-bottom:8px; border-bottom:1px solid var(--accent); padding-bottom:4px; }
  .subtitle { color:var(--dim); font-size:.85em; margin-bottom:16px; }
  .card { background:var(--card); border-radius:8px; padding:14px; margin-bottom:14px; }
  .grid { display:grid; gap:8px; }
  .grid-2 { grid-template-columns:1fr 1fr; }
  .grid-3 { grid-template-columns:1fr 1fr 1fr; }
  label { font-size:.85em; color:var(--dim); display:block; margin-bottom:2px; }
  input,select { width:100%; padding:8px; border:1px solid var(--accent); border-radius:4px; background:#0d1b36; color:var(--text); font-size:.9em; }
  input:focus,select:focus { outline:none; border-color:var(--hl); }
  button,.btn { padding:8px 14px; border:none; border-radius:4px; font-size:.85em; cursor:pointer; font-weight:600; transition:opacity .2s; }
  button:hover,.btn:hover { opacity:.85; }
  .btn-primary { background:var(--hl); color:#fff; }
  .btn-secondary { background:var(--accent); color:var(--text); }
  .btn-success { background:var(--ok); color:#fff; }
  .btn-danger { background:var(--err); color:#fff; }
  .btn-sm { padding:6px 10px; font-size:.8em; }
  .btn-block { width:100%; }
  .status-bar { display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:6px; }
  .status-dot { width:8px; height:8px; border-radius:50%; display:inline-block; margin-right:4px; }
  .status-dot.on { background:var(--ok); }
  .status-dot.off { background:var(--err); }
  .status-dot.warn { background:#f39c12; }
  .badge { display:inline-block; padding:2px 8px; border-radius:10px; font-size:.75em; font-weight:600; }
  .badge-ok { background:var(--ok); color:#fff; }
  .badge-warn { background:#f39c12; color:#000; }
  .badge-err { background:var(--err); color:#fff; }
  #log { max-height:300px; overflow-y:auto; font-family:'Courier New',monospace; font-size:.8em; line-height:1.6; }
  #log .entry { padding:2px 0; border-bottom:1px solid rgba(255,255,255,.05); }
  #log .rx { color:#3498db; }
  #log .tx { color:var(--ok); }
  #log .ts { color:var(--dim); margin-right:6px; }
  .toast { position:fixed; bottom:20px; right:20px; padding:10px 18px; border-radius:6px; color:#fff; font-size:.85em; z-index:9999; animation:fadeIn .3s; }
  @keyframes fadeIn { from{opacity:0;transform:translateY(10px)} to{opacity:1;transform:translateY(0)} }
  .flex-row { display:flex; gap:6px; flex-wrap:wrap; }
  .mb-8 { margin-bottom:8px; }
  .mt-8 { margin-top:8px; }
  .hidden { display:none; }
  .tab-bar { display:flex; gap:4px; margin-bottom:14px; }
  .tab { padding:8px 16px; border-radius:6px 6px 0 0; background:var(--accent); color:var(--dim); cursor:pointer; font-size:.9em; font-weight:600; border:none; }
  .tab.active { background:var(--card); color:var(--text); }
  .section { display:none; }
  .section.active { display:block; }
  .raw-input { display:flex; gap:6px; }
  .raw-input input { flex:1; }
  @media(max-width:500px) { .grid-2,.grid-3 { grid-template-columns:1fr; } }
</style>
</head>
<body>

<h1>CEC Dongle</h1>
<div class="subtitle" id="statusLine">Loading...</div>

<div class="tab-bar">
  <button class="tab active" onclick="showTab('control')">Control</button>
  <button class="tab" onclick="showTab('log')">Log</button>
  <button class="tab" onclick="showTab('config')">Config</button>
  <button class="tab" onclick="showTab('wifi')">WiFi</button>
</div>

<!-- ─── Control Tab ─── -->
<div id="tab-control" class="section active">
  <div class="card">
    <h2>Power</h2>
    <div class="grid grid-3">
      <button class="btn btn-success" onclick="api('/api/cec/power/on','POST')">TV On</button>
      <button class="btn btn-danger" onclick="api('/api/cec/power/off','POST')">Standby All</button>
      <button class="btn btn-secondary" onclick="api('/api/cec/source/active','POST')">Active Source</button>
    </div>
  </div>

  <div class="card">
    <h2>Volume</h2>
    <div class="grid grid-3">
      <button class="btn btn-secondary" onclick="api('/api/cec/volume/up','POST')">Vol +</button>
      <button class="btn btn-secondary" onclick="api('/api/cec/volume/down','POST')">Vol −</button>
      <button class="btn btn-secondary" onclick="api('/api/cec/volume/mute','POST')">Mute</button>
    </div>
  </div>

  <div class="card">
    <h2>Send Raw CEC</h2>
    <div class="grid grid-2 mb-8">
      <div><label>Source (hex)</label><input id="rawSrc" value="" placeholder="auto"></div>
      <div><label>Destination (hex)</label><input id="rawDst" value="0" placeholder="0=TV, F=Broadcast"></div>
    </div>
    <label>Data bytes (hex, comma-separated)</label>
    <div class="raw-input">
      <input id="rawData" placeholder="e.g. 36 or 44,41">
      <button class="btn btn-primary" onclick="sendRaw()">Send</button>
    </div>
  </div>

  <div class="card">
    <h2>Switch Input</h2>
    <div class="grid grid-2 mb-8">
      <div><label>Physical Address (hex)</label><input id="inputPA" value="1000" placeholder="e.g. 1000 = HDMI1"></div>
      <button class="btn btn-secondary mt-8" onclick="switchInput()" style="align-self:end;">Switch</button>
    </div>
  </div>
</div>

<!-- ─── Log Tab ─── -->
<div id="tab-log" class="section">
  <div class="card">
    <div class="status-bar mb-8">
      <h2 style="border:none;margin:0;padding:0;">CEC Bus Log</h2>
      <div>
        <button class="btn btn-sm btn-secondary" onclick="refreshLog()">Refresh</button>
        <button class="btn btn-sm btn-danger" onclick="clearLog()">Clear</button>
        <label style="display:inline;margin-left:8px;"><input type="checkbox" id="autoRefresh" checked onchange="toggleAutoRefresh()"> Auto</label>
      </div>
    </div>
    <div id="log"></div>
  </div>
</div>

<!-- ─── Config Tab ─── -->
<div id="tab-config" class="section">
  <div class="card">
    <h2>CEC Settings</h2>
    <div class="grid grid-2 mb-8">
      <div><label>GPIO Pin</label><input id="cfgPin" type="number"></div>
      <div><label>Logical Address (hex)</label><input id="cfgAddr"></div>
      <div><label>Physical Address (hex)</label><input id="cfgPhys"></div>
      <div><label>OSD Name</label><input id="cfgOsd"></div>
    </div>
    <div class="grid grid-2 mb-8">
      <label><input type="checkbox" id="cfgPromisc"> Promiscuous Mode</label>
      <label><input type="checkbox" id="cfgMonitor"> Monitor Mode</label>
    </div>
    <div class="grid grid-2 mb-8">
      <div><label>Hostname</label><input id="cfgHostname"></div>
      <div><label>Log Buffer Size</label><input id="cfgLogBuf" type="number"></div>
    </div>
    <div class="flex-row">
      <button class="btn btn-primary" onclick="saveConfig()">Save Config</button>
      <button class="btn btn-secondary" onclick="loadConfig()">Reload</button>
      <button class="btn btn-danger" onclick="resetConfig()">Factory Reset</button>
      <button class="btn btn-secondary" onclick="restartDevice()">Restart Device</button>
    </div>
  </div>
</div>

<!-- ─── WiFi Tab ─── -->
<div id="tab-wifi" class="section">
  <div class="card">
    <h2>WiFi Configuration</h2>
    <div class="mb-8">
      <button class="btn btn-secondary btn-sm" onclick="scanWifi()">Scan Networks</button>
    </div>
    <div id="wifiList" class="mb-8"></div>
    <div class="grid grid-2 mb-8">
      <div><label>SSID</label><input id="wifiSsid"></div>
      <div><label>Password</label><input id="wifiPass" type="password"></div>
    </div>
    <button class="btn btn-primary" onclick="connectWifi()">Connect</button>
  </div>
</div>

<script>
// ─── Helpers ────────────────────────────────────────────────────────────────
function $(id){ return document.getElementById(id); }
function toast(msg, ok=true){
  let t=document.createElement('div');
  t.className='toast';
  t.style.background=ok?'#27ae60':'#c0392b';
  t.textContent=msg;
  document.body.appendChild(t);
  setTimeout(()=>t.remove(),3000);
}

async function api(url, method='GET', body=null){
  try{
    let opts={method, headers:{'Content-Type':'application/json'}};
    if(body) opts.body=JSON.stringify(body);
    let r=await fetch(url, opts);
    let j=await r.json();
    if(j.success===false) toast(j.error||'Failed',false);
    else if(j.message) toast(j.message);
    else if(j.success) toast('OK');
    return j;
  }catch(e){ toast('Network error',false); }
}

// ─── Tabs ───────────────────────────────────────────────────────────────────
function showTab(name){
  document.querySelectorAll('.section').forEach(s=>s.classList.remove('active'));
  document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));
  $('tab-'+name).classList.add('active');
  event.target.classList.add('active');
  if(name==='log') refreshLog();
  if(name==='config') loadConfig();
}

// ─── Status ─────────────────────────────────────────────────────────────────
async function refreshStatus(){
  let s=await api('/api/status');
  if(!s) return;
  let dot = s.wifi_status==='Connected'?'on':'warn';
  $('statusLine').innerHTML=`<span class="status-dot ${dot}"></span>${s.wifi_status} · ${s.wifi_ip} · Heap: ${s.heap_free}B · Up: ${(s.uptime_ms/1000/60).toFixed(1)}m`;
}

// ─── Log ────────────────────────────────────────────────────────────────────
async function refreshLog(){
  let entries=await api('/api/cec/log');
  if(!entries||!Array.isArray(entries)) return;
  let html='';
  entries.forEach(e=>{
    let cls=e.dir==='rx'?'rx':'tx';
    let ts=(e.t/1000).toFixed(1)+'s';
    html+=`<div class="entry ${cls}"><span class="ts">${ts}</span>[${e.dir.toUpperCase()}] ${e.hex} — ${e.msg}</div>`;
  });
  $('log').innerHTML=html||'<div style="color:var(--dim)">No messages yet.</div>';
  $('log').scrollTop=$('log').scrollHeight;
}
async function clearLog(){ await api('/api/cec/log','DELETE'); refreshLog(); }
let autoTimer=null;
function toggleAutoRefresh(){
  if($('autoRefresh').checked){ autoTimer=setInterval(refreshLog,2000); }
  else{ clearInterval(autoTimer); }
}

// ─── Control ────────────────────────────────────────────────────────────────
function sendRaw(){
  let src=$('rawSrc').value.trim();
  let dst=parseInt($('rawDst').value.trim(),16)||0;
  let dataStr=$('rawData').value.trim().replace(/\s+/g,',');
  let bytes=dataStr.split(',').filter(s=>s).map(s=>parseInt(s,16));
  if(!bytes.length) return toast('Enter data bytes',false);
  let body={destination:dst, data:bytes};
  if(src) body.source=parseInt(src,16);
  api('/api/cec/send','POST',body);
}
function switchInput(){
  let pa=parseInt($('inputPA').value.trim(),16)||0;
  api('/api/cec/input','POST',{physical_address:pa});
}

// ─── Config ─────────────────────────────────────────────────────────────────
async function loadConfig(){
  let c=await api('/api/config');
  if(!c) return;
  $('cfgPin').value=c.cec_pin;
  $('cfgAddr').value='0x'+c.cec_address.toString(16).toUpperCase();
  $('cfgPhys').value='0x'+c.cec_physical.toString(16).toUpperCase().padStart(4,'0');
  $('cfgOsd').value=c.cec_osd_name;
  $('cfgPromisc').checked=c.cec_promiscuous;
  $('cfgMonitor').checked=c.cec_monitor_mode;
  $('cfgHostname').value=c.hostname;
  $('cfgLogBuf').value=c.log_buffer_size;
}
function saveConfig(){
  let body={
    cec_pin:parseInt($('cfgPin').value),
    cec_address:parseInt($('cfgAddr').value),
    cec_physical:parseInt($('cfgPhys').value),
    cec_osd_name:$('cfgOsd').value,
    cec_promiscuous:$('cfgPromisc').checked,
    cec_monitor_mode:$('cfgMonitor').checked,
    hostname:$('cfgHostname').value,
    log_buffer_size:parseInt($('cfgLogBuf').value)
  };
  api('/api/config','POST',body);
}
function resetConfig(){ if(confirm('Factory reset?')) api('/api/system/reset','POST'); }
function restartDevice(){ if(confirm('Restart device?')) api('/api/system/restart','POST'); }

// ─── WiFi ───────────────────────────────────────────────────────────────────
async function scanWifi(){
  $('wifiList').innerHTML='Scanning...';
  let nets=await api('/api/wifi/scan');
  if(!nets||!Array.isArray(nets)){ $('wifiList').innerHTML='Scan failed'; return; }
  let html='';
  nets.sort((a,b)=>b.rssi-a.rssi);
  nets.forEach(n=>{
    let bars=n.rssi>-50?'▉▉▉▉':n.rssi>-65?'▉▉▉':n.rssi>-75?'▉▉':'▉';
    let lock=n.encrypted?'🔒':'';
    html+=`<div style="padding:6px;cursor:pointer;border-bottom:1px solid rgba(255,255,255,.05)" onclick="$('wifiSsid').value='${n.ssid}'">${lock} <strong>${n.ssid}</strong> <span style="color:var(--dim)">${n.rssi}dBm ${bars}</span></div>`;
  });
  $('wifiList').innerHTML=html||'No networks found.';
}
function connectWifi(){
  let ssid=$('wifiSsid').value;
  let pass=$('wifiPass').value;
  if(!ssid) return toast('Enter SSID',false);
  api('/api/wifi/connect','POST',{ssid,password:pass});
}

// ─── Init ───────────────────────────────────────────────────────────────────
refreshStatus();
setInterval(refreshStatus,5000);
autoTimer=setInterval(refreshLog,2000);
</script>
</body>
</html>
)rawliteral";
