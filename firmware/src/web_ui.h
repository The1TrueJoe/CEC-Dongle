/*
 * Embedded Web UI — single-page CEC control + debug tool served from PROGMEM
 * All logic client-side. Zero external dependencies. Dark-themed, responsive.
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
*{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#111318;--card:#181b22;--card2:#1e222b;--border:#272c38;
  --accent:#3b82f6;--hl:#ef4444;--ok:#22c55e;--warn:#f59e0b;--text:#e4e4e7;
  --dim:#71717a;--mono:'SF Mono',SFMono-Regular,'Cascadia Code',Consolas,'Liberation Mono',Menlo,monospace;
  --sans:-apple-system,BlinkMacSystemFont,'Segoe UI','Inter',Roboto,sans-serif;
  --r:6px;
}
html{font-size:14px}
body{font-family:var(--sans);background:var(--bg);color:var(--text);padding:0;min-height:100vh}

/* ── Layout ── */
.app{display:flex;flex-direction:column;min-height:100vh}
header{background:var(--card);border-bottom:1px solid var(--border);padding:10px 16px;display:flex;align-items:center;gap:12px;position:sticky;top:0;z-index:100}
header h1{font-size:1rem;font-weight:700;letter-spacing:-.02em;white-space:nowrap}
header .dot{width:7px;height:7px;border-radius:50%;flex-shrink:0}
header .dot.on{background:var(--ok);box-shadow:0 0 6px var(--ok)}
header .dot.off{background:var(--hl);box-shadow:0 0 6px var(--hl)}
header .dot.warn{background:var(--warn);box-shadow:0 0 6px var(--warn)}
#hdrInfo{font-size:.75rem;color:var(--dim);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;flex:1}

.tabs{display:flex;background:var(--card);border-bottom:1px solid var(--border);padding:0 8px;overflow-x:auto;-webkit-overflow-scrolling:touch}
.tabs button{background:0;border:0;color:var(--dim);padding:10px 14px;font:inherit;font-size:.8rem;font-weight:600;cursor:pointer;white-space:nowrap;border-bottom:2px solid transparent;transition:color .15s,border-color .15s}
.tabs button:hover{color:var(--text)}
.tabs button.on{color:var(--accent);border-bottom-color:var(--accent)}

main{flex:1;padding:12px;max-width:960px;width:100%;margin:0 auto}
.view{display:none}.view.on{display:block}

/* ── Cards & Controls ── */
.c{background:var(--card);border:1px solid var(--border);border-radius:var(--r);padding:12px;margin-bottom:12px}
.c h3{font-size:.8rem;font-weight:600;color:var(--dim);text-transform:uppercase;letter-spacing:.05em;margin-bottom:8px}
.g{display:grid;gap:6px}
.g2{grid-template-columns:1fr 1fr}.g3{grid-template-columns:1fr 1fr 1fr}.g4{grid-template-columns:1fr 1fr 1fr 1fr}
.g5{grid-template-columns:repeat(5,1fr)}

label{font-size:.75rem;color:var(--dim);display:block;margin-bottom:2px}
input,select,textarea{width:100%;padding:7px 8px;border:1px solid var(--border);border-radius:var(--r);background:var(--bg);color:var(--text);font:inherit;font-size:.85rem;transition:border-color .15s}
input:focus,select:focus,textarea:focus{outline:0;border-color:var(--accent)}
textarea{resize:vertical;font-family:var(--mono);font-size:.78rem}
input[type=checkbox]{width:auto;margin-right:4px;accent-color:var(--accent)}

.btn{display:inline-flex;align-items:center;justify-content:center;gap:4px;padding:7px 12px;border:1px solid var(--border);border-radius:var(--r);background:var(--card2);color:var(--text);font:inherit;font-size:.8rem;font-weight:600;cursor:pointer;transition:background .12s,border-color .12s;white-space:nowrap;user-select:none}
.btn:hover{background:var(--border)}.btn:active{transform:scale(.97)}
.btn-p{background:var(--accent);border-color:var(--accent);color:#fff}.btn-p:hover{background:#2563eb}
.btn-ok{background:var(--ok);border-color:var(--ok);color:#fff}.btn-ok:hover{background:#16a34a}
.btn-d{background:var(--hl);border-color:var(--hl);color:#fff}.btn-d:hover{background:#dc2626}
.btn-w{background:var(--warn);border-color:var(--warn);color:#000}.btn-w:hover{background:#d97706}
.btn-sm{padding:4px 8px;font-size:.72rem}
.btn-xs{padding:2px 6px;font-size:.68rem;border-radius:4px}
.btn-block{width:100%}

.pill{display:inline-block;padding:1px 7px;border-radius:10px;font-size:.68rem;font-weight:600;line-height:1.6}
.pill-rx{background:#1e3a5f;color:#60a5fa}.pill-tx{background:#14412a;color:#4ade80}

.row{display:flex;gap:6px;flex-wrap:wrap;align-items:center}
.row-between{justify-content:space-between}
.mb{margin-bottom:8px}.mt{margin-top:8px}

/* ── Remote ── */
.remote{background:var(--card2);border:1px solid var(--border);border-radius:12px;padding:16px;max-width:280px;margin:0 auto}
.remote .rr{display:flex;justify-content:center;gap:6px;margin-bottom:6px}
.rk{width:52px;height:38px;border:1px solid var(--border);border-radius:var(--r);background:var(--bg);color:var(--text);font:inherit;font-size:.72rem;font-weight:600;cursor:pointer;display:flex;align-items:center;justify-content:center;transition:background .1s}
.rk:hover{background:var(--border)}.rk:active{background:var(--accent);color:#fff}
.rk.pwr-on{border-color:var(--ok);color:var(--ok)}.rk.pwr-off{border-color:var(--hl);color:var(--hl)}
.rk-w{width:72px}.rk-nav{width:42px;height:42px;border-radius:50%}
.nav-grid{display:grid;grid-template-columns:42px 42px 42px;grid-template-rows:42px 42px 42px;gap:4px;justify-content:center;margin:8px 0}
.nav-grid .center{background:var(--accent);color:#fff;font-size:.65rem;border-color:var(--accent)}

/* ── Bus monitor ── */
#logWrap{max-height:480px;overflow-y:auto;font-family:var(--mono);font-size:.75rem;line-height:1.7;scrollbar-width:thin;scrollbar-color:var(--border) transparent}
#logWrap::-webkit-scrollbar{width:5px}#logWrap::-webkit-scrollbar-track{background:0}#logWrap::-webkit-scrollbar-thumb{background:var(--border);border-radius:3px}
.le{padding:3px 6px;border-bottom:1px solid rgba(255,255,255,.03);display:flex;gap:8px;align-items:baseline}
.le:hover{background:rgba(255,255,255,.02)}
.le .ts{color:var(--dim);min-width:56px;text-align:right;flex-shrink:0}
.le .hex{color:var(--warn);min-width:80px;flex-shrink:0}
.le .msg{color:var(--text);flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.le .copy-btn{opacity:0;transition:opacity .15s;cursor:pointer;background:0;border:0;color:var(--dim);font-size:.7rem;padding:0 4px}
.le:hover .copy-btn{opacity:1}

.filter-bar{display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin-bottom:8px}
.filter-bar input,.filter-bar select{width:auto;min-width:0;flex:0 0 auto;padding:4px 6px;font-size:.75rem}
.filter-bar input{max-width:120px}

.stats{display:flex;gap:12px;margin-bottom:8px;font-size:.75rem;color:var(--dim)}
.stats span{display:flex;align-items:center;gap:3px}
.stats .num{font-weight:700;color:var(--text);font-family:var(--mono)}

/* ── Opcode builder ── */
.builder-preview{background:var(--bg);border:1px solid var(--border);border-radius:var(--r);padding:6px 8px;font-family:var(--mono);font-size:.8rem;color:var(--warn);min-height:28px;word-break:break-all;margin-top:4px}

/* ── Device map ── */
.devmap{display:flex;flex-wrap:wrap;gap:8px}
.dev-chip{background:var(--card2);border:1px solid var(--border);border-radius:var(--r);padding:8px 10px;font-size:.78rem;min-width:120px}
.dev-chip .da{font-family:var(--mono);color:var(--accent);font-weight:700;font-size:.7rem}
.dev-chip .dn{font-weight:600;margin-top:2px}
.dev-chip .dp{font-size:.68rem;color:var(--dim)}

/* ── Toast ── */
.toast{position:fixed;bottom:16px;right:16px;padding:8px 16px;border-radius:var(--r);color:#fff;font-size:.8rem;z-index:9999;animation:toastIn .25s}
@keyframes toastIn{from{opacity:0;transform:translateY(8px)}to{opacity:1;transform:translateY(0)}}

/* ── WiFi list ── */
.wnet{padding:8px;cursor:pointer;border-bottom:1px solid var(--border);display:flex;justify-content:space-between;align-items:center;transition:background .1s}
.wnet:hover{background:var(--card2)}
.wnet .ws{font-size:.7rem;color:var(--dim)}

/* ── Responsive ── */
@media(max-width:600px){
  .g2,.g3,.g4{grid-template-columns:1fr}
  .g5{grid-template-columns:repeat(3,1fr)}
  .remote{max-width:100%}
  #logWrap{max-height:350px}
}
</style>
</head>
<body>
<div class="app">

<header>
  <span class="dot off" id="hdrDot"></span>
  <h1>CEC Dongle</h1>
  <span id="hdrInfo">Connecting...</span>
</header>

<div class="tabs" id="tabBar">
  <button class="on" data-v="remote">Remote</button>
  <button data-v="control">Control</button>
  <button data-v="monitor">Monitor</button>
  <button data-v="builder">Send</button>
  <button data-v="devices">Devices</button>
  <button data-v="config">Config</button>
  <button data-v="wifi">WiFi</button>
</div>

<main>

<!-- ═══════ REMOTE ═══════ -->
<div class="view on" id="v-remote">
  <div class="remote">
    <div class="rr">
      <button class="rk pwr-on rk-w" onclick="cecSend(0,{d:[0x04]})">ON</button>
      <button class="rk pwr-off rk-w" onclick="cecSend(0,{d:[0x36]})">OFF</button>
      <button class="rk rk-w" onclick="cecSend(5,{d:[0x44,0x43]})">MUTE</button>
    </div>
    <div class="rr">
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x40]})">SRC</button>
      <button class="rk" onclick="cecSend(15,{d:[0x82,ph(1),ph(0)]})">INPUT</button>
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x0E]})">GUIDE</button>
    </div>
    <div class="nav-grid">
      <div></div>
      <button class="rk rk-nav" onclick="cecSend(0,{d:[0x44,0x01]})">&#9650;</button>
      <div></div>
      <button class="rk rk-nav" onclick="cecSend(0,{d:[0x44,0x03]})">&#9664;</button>
      <button class="rk rk-nav center" onclick="cecSend(0,{d:[0x44,0x00]})">OK</button>
      <button class="rk rk-nav" onclick="cecSend(0,{d:[0x44,0x04]})">&#9654;</button>
      <div></div>
      <button class="rk rk-nav" onclick="cecSend(0,{d:[0x44,0x02]})">&#9660;</button>
      <div></div>
    </div>
    <div class="rr">
      <button class="rk rk-w" onclick="cecSend(0,{d:[0x44,0x09]})">MENU</button>
      <button class="rk rk-w" onclick="cecSend(0,{d:[0x44,0x0D]})">BACK</button>
      <button class="rk rk-w" onclick="cecSend(0,{d:[0x44,0x35]})">INFO</button>
    </div>
    <div class="rr" style="margin-top:4px">
      <button class="rk" onclick="cecSend(5,{d:[0x44,0x41]})">V+</button>
      <button class="rk" onclick="cecSend(5,{d:[0x44,0x42]})">V-</button>
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x34]})">CH+</button>
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x30]})">CH-</button>
    </div>
    <div class="rr" style="margin-top:4px">
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x44]})">&#9654;&#10073;&#10073;</button>
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x45]})">&#9632;</button>
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x48]})">&#9668;&#9668;</button>
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x49]})">&#9658;&#9658;</button>
    </div>
    <div class="g g5" style="margin-top:8px">
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x21]})">1</button>
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x22]})">2</button>
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x23]})">3</button>
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x24]})">4</button>
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x25]})">5</button>
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x26]})">6</button>
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x27]})">7</button>
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x28]})">8</button>
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x29]})">9</button>
      <button class="rk" onclick="cecSend(0,{d:[0x44,0x20]})">0</button>
    </div>
    <div class="rr" style="margin-top:8px">
      <select id="rmtTarget" style="flex:1;font-size:.75rem;padding:4px">
        <option value="0">0 - TV</option>
        <option value="1">1 - Recording 1</option>
        <option value="4">4 - Playback 1</option>
        <option value="5" selected>5 - Audio System</option>
        <option value="8">8 - Playback 2</option>
        <option value="15">F - Broadcast</option>
      </select>
    </div>
  </div>
</div>

<!-- ═══════ CONTROL ═══════ -->
<div class="view" id="v-control">
  <div class="c">
    <h3>Power</h3>
    <div class="g g4">
      <button class="btn btn-ok" onclick="api('/api/cec/power/on','POST')">TV On</button>
      <button class="btn btn-d" onclick="api('/api/cec/power/off','POST')">Standby All</button>
      <button class="btn btn-p" onclick="api('/api/cec/source/active','POST')">Active Src</button>
      <button class="btn" onclick="cecSend(15,{d:[0x85]})">Req Active</button>
    </div>
  </div>
  <div class="c">
    <h3>Volume &amp; Audio</h3>
    <div class="g g4">
      <button class="btn" onclick="api('/api/cec/volume/up','POST')">Vol +</button>
      <button class="btn" onclick="api('/api/cec/volume/down','POST')">Vol &minus;</button>
      <button class="btn" onclick="api('/api/cec/volume/mute','POST')">Mute</button>
      <button class="btn" onclick="cecSend(5,{d:[0x7D]})">Audio?</button>
    </div>
  </div>
  <div class="c">
    <h3>HDMI Inputs</h3>
    <div class="g g4">
      <button class="btn" onclick="setInput(0x1000)">HDMI 1</button>
      <button class="btn" onclick="setInput(0x2000)">HDMI 2</button>
      <button class="btn" onclick="setInput(0x3000)">HDMI 3</button>
      <button class="btn" onclick="setInput(0x4000)">HDMI 4</button>
    </div>
    <div class="row mt">
      <label style="margin:0">Custom PA (hex):</label>
      <input id="customPA" value="" placeholder="e.g. 2100" style="width:80px">
      <button class="btn btn-sm" onclick="setInput(parseInt($('customPA').value,16))">Go</button>
    </div>
  </div>
  <div class="c">
    <h3>CEC Queries</h3>
    <div class="g g3">
      <button class="btn btn-sm" onclick="cecSend(0,{d:[0x8F]})">TV Power?</button>
      <button class="btn btn-sm" onclick="cecSend(0,{d:[0x46]})">TV OSD?</button>
      <button class="btn btn-sm" onclick="cecSend(0,{d:[0x9F]})">TV CEC Ver?</button>
      <button class="btn btn-sm" onclick="cecSend(0,{d:[0x83]})">TV Phys Addr?</button>
      <button class="btn btn-sm" onclick="cecSend(0,{d:[0x8C]})">TV Vendor?</button>
      <button class="btn btn-sm" onclick="cecSend(0,{d:[0x91]})">TV Language?</button>
      <button class="btn btn-sm" onclick="cecSend(5,{d:[0x8F]})">AVR Power?</button>
      <button class="btn btn-sm" onclick="cecSend(5,{d:[0x46]})">AVR OSD?</button>
      <button class="btn btn-sm" onclick="cecSend(5,{d:[0x7D]})">Sys Audio?</button>
    </div>
  </div>
  <div class="c">
    <h3>Broadcast Commands</h3>
    <div class="g g3">
      <button class="btn btn-d btn-sm" onclick="cecSend(15,{d:[0x36]})">Standby All</button>
      <button class="btn btn-sm" onclick="cecSend(15,{d:[0x04]})">Image View On</button>
      <button class="btn btn-sm" onclick="cecSend(15,{d:[0x85]})">Req Active Src</button>
      <button class="btn btn-sm" onclick="cecSend(15,{d:[0x86,ph(1),ph(0)]})">Set Stream</button>
    </div>
  </div>
</div>

<!-- ═══════ MONITOR ═══════ -->
<div class="view" id="v-monitor">
  <div class="c">
    <div class="row row-between mb">
      <h3 style="margin:0">Bus Monitor</h3>
      <div class="row">
        <button class="btn btn-xs" onclick="togglePause()" id="pauseBtn">Pause</button>
        <button class="btn btn-xs" onclick="clearLog()">Clear</button>
        <button class="btn btn-xs" onclick="exportLog()">Export</button>
        <span style="font-size:.68rem;color:var(--dim)">
          <input type="checkbox" id="autoScroll" checked style="margin:0"> scroll
        </span>
      </div>
    </div>
    <div class="filter-bar">
      <select id="fDir"><option value="">dir</option><option value="rx">RX</option><option value="tx">TX</option></select>
      <input id="fSrc" placeholder="src addr" style="width:64px">
      <input id="fDst" placeholder="dst addr" style="width:64px">
      <input id="fOp" placeholder="opcode" style="width:64px">
      <input id="fText" placeholder="search text..." style="width:140px">
      <button class="btn btn-xs" onclick="clearFilters()">Reset</button>
    </div>
    <div class="stats" id="logStats">
      <span>Total: <b class="num" id="statTotal">0</b></span>
      <span>RX: <b class="num" id="statRx">0</b></span>
      <span>TX: <b class="num" id="statTx">0</b></span>
    </div>
    <div id="logWrap"></div>
  </div>
</div>

<!-- ═══════ BUILDER (Send) ═══════ -->
<div class="view" id="v-builder">
  <div class="c">
    <h3>CEC Command Builder</h3>
    <div class="g g2 mb">
      <div>
        <label>Source</label>
        <select id="bSrc">
          <option value="">Auto (dongle)</option>
          <option value="0">0 - TV</option><option value="1">1 - Recording 1</option>
          <option value="3">3 - Tuner 1</option><option value="4">4 - Playback 1</option>
          <option value="5">5 - Audio Sys</option><option value="8">8 - Playback 2</option>
          <option value="14">E - Free Use</option>
        </select>
      </div>
      <div>
        <label>Destination</label>
        <select id="bDst">
          <option value="0">0 - TV</option><option value="1">1 - Recording 1</option>
          <option value="3">3 - Tuner 1</option><option value="4">4 - Playback 1</option>
          <option value="5">5 - Audio Sys</option><option value="8">8 - Playback 2</option>
          <option value="14">E - Free Use</option><option value="15">F - Broadcast</option>
        </select>
      </div>
    </div>
    <div class="mb">
      <label>Opcode</label>
      <select id="bOpcode" onchange="updateBuilder()">
        <option value="">-- select --</option>
        <option value="04">04 - Image View On</option>
        <option value="0D">0D - Text View On</option>
        <option value="36">36 - Standby</option>
        <option value="44">44 - User Control Pressed</option>
        <option value="45">45 - User Control Released</option>
        <option value="46">46 - Give OSD Name</option>
        <option value="47">47 - Set OSD Name</option>
        <option value="70">70 - System Audio Mode Request</option>
        <option value="72">72 - Set System Audio Mode</option>
        <option value="7D">7D - Give System Audio Mode Status</option>
        <option value="82">82 - Active Source</option>
        <option value="83">83 - Give Physical Address</option>
        <option value="84">84 - Report Physical Address</option>
        <option value="85">85 - Request Active Source</option>
        <option value="86">86 - Set Stream Path</option>
        <option value="8C">8C - Give Device Vendor ID</option>
        <option value="8D">8D - Menu Request</option>
        <option value="8F">8F - Give Device Power Status</option>
        <option value="90">90 - Report Power Status</option>
        <option value="91">91 - Get Menu Language</option>
        <option value="9E">9E - CEC Version</option>
        <option value="9F">9F - Get CEC Version</option>
      </select>
    </div>
    <div class="mb">
      <label>Operand bytes (hex, space-separated, optional)</label>
      <input id="bOperands" placeholder="e.g. 41 for Vol Up with opcode 44" oninput="updateBuilder()">
    </div>
    <div class="mb">
      <label>Preview</label>
      <div class="builder-preview" id="bPreview">--</div>
    </div>
    <button class="btn btn-p btn-block" onclick="sendBuilt()">Send Command</button>
  </div>
  <div class="c">
    <h3>Raw Hex Send</h3>
    <label>Full frame bytes (hex, space or colon separated — header is auto-prefixed)</label>
    <div class="row mb">
      <input id="rawHex" placeholder="e.g. 36  or  44 41  or  82 10 00" style="flex:1">
      <select id="rawDst" style="width:auto">
        <option value="0">-&gt; TV</option>
        <option value="5">-&gt; Audio</option>
        <option value="4">-&gt; Play1</option>
        <option value="8">-&gt; Play2</option>
        <option value="15">-&gt; Bcast</option>
      </select>
      <button class="btn btn-p" onclick="sendRawHex()">Send</button>
    </div>
  </div>
  <div class="c">
    <h3>Common User Control Codes</h3>
    <div style="font-size:.72rem;color:var(--dim);font-family:var(--mono);columns:2;column-gap:16px;line-height:1.8">
      00 Select &middot; 01 Up &middot; 02 Down &middot; 03 Left &middot; 04 Right<br>
      09 Root Menu &middot; 0D Exit &middot; 0E Guide &middot; 20-29 Digit 0-9<br>
      30 Ch Up &middot; 31 Ch Down &middot; 32 Prev Ch &middot; 35 Info<br>
      40 Power &middot; 41 Vol Up &middot; 42 Vol Down &middot; 43 Mute<br>
      44 Play &middot; 45 Stop &middot; 46 Pause &middot; 47 Record<br>
      48 Rewind &middot; 49 Fast Fwd &middot; 4B Skip Fwd &middot; 4C Skip Rev<br>
      6D Power On &middot; 6C Power Off
    </div>
  </div>
</div>

<!-- ═══════ DEVICES ═══════ -->
<div class="view" id="v-devices">
  <div class="c">
    <div class="row row-between mb">
      <h3 style="margin:0">CEC Device Discovery</h3>
      <button class="btn btn-sm btn-p" onclick="scanDevices()">Scan Bus</button>
    </div>
    <p style="font-size:.75rem;color:var(--dim)" class="mb">Polls logical addresses 0-14 for OSD name and power status. Check the monitor tab for responses.</p>
    <div class="devmap" id="devMap">
      <div style="color:var(--dim);font-size:.8rem">Click "Scan Bus" to discover devices.</div>
    </div>
  </div>
  <div class="c">
    <h3>CEC Address Reference</h3>
    <div style="font-size:.72rem;font-family:var(--mono);line-height:1.8;columns:2;column-gap:16px;color:var(--dim)">
      0 TV &middot; 1 Recording 1 &middot; 2 Recording 2 &middot; 3 Tuner 1<br>
      4 Playback 1 &middot; 5 Audio System &middot; 6 Tuner 2 &middot; 7 Tuner 3<br>
      8 Playback 2 &middot; 9 Recording 3 &middot; A Tuner 4<br>
      B-D Reserved &middot; E Free Use &middot; F Broadcast
    </div>
  </div>
  <div class="c">
    <h3>Physical Address Map</h3>
    <div style="font-size:.72rem;font-family:var(--mono);line-height:1.8;color:var(--dim)">
      x.0.0.0 = HDMI input x on TV<br>
      x.y.0.0 = HDMI input y on device at x.0.0.0<br>
      Example: 2.1.0.0 = HDMI1 on AVR which is on HDMI2 of TV
    </div>
  </div>
</div>

<!-- ═══════ CONFIG ═══════ -->
<div class="view" id="v-config">
  <div class="c">
    <h3>CEC Settings</h3>
    <div class="g g2 mb">
      <div><label>GPIO Pin</label><input id="cfgPin" type="number"></div>
      <div><label>Logical Address (hex)</label><input id="cfgAddr"></div>
      <div><label>Physical Address (hex)</label><input id="cfgPhys"></div>
      <div><label>OSD Name</label><input id="cfgOsd"></div>
    </div>
    <div class="g g2 mb">
      <label><input type="checkbox" id="cfgPromisc"> Promiscuous Mode (see all traffic)</label>
      <label><input type="checkbox" id="cfgMonitor"> Monitor Mode (read-only, no ACK)</label>
    </div>
  </div>
  <div class="c">
    <h3>Device Settings</h3>
    <div class="g g2 mb">
      <div><label>Hostname</label><input id="cfgHostname"></div>
      <div><label>Log Buffer Size</label><input id="cfgLogBuf" type="number"></div>
    </div>
  </div>
  <div class="c">
    <div class="row">
      <button class="btn btn-p" onclick="saveConfig()">Save Config</button>
      <button class="btn" onclick="loadConfig()">Reload</button>
      <button class="btn btn-w" onclick="restartDevice()">Restart</button>
      <button class="btn btn-d" onclick="resetConfig()">Factory Reset</button>
    </div>
  </div>
  <div class="c">
    <h3>System Info</h3>
    <div id="sysInfo" style="font-size:.78rem;font-family:var(--mono);color:var(--dim);line-height:1.8">Loading...</div>
  </div>
</div>

<!-- ═══════ WIFI ═══════ -->
<div class="view" id="v-wifi">
  <div class="c">
    <div class="row row-between mb">
      <h3 style="margin:0">WiFi Networks</h3>
      <button class="btn btn-sm" onclick="scanWifi()">Scan</button>
    </div>
    <div id="wifiList" style="max-height:250px;overflow-y:auto"></div>
  </div>
  <div class="c">
    <h3>Connect</h3>
    <div class="g g2 mb">
      <div><label>SSID</label><input id="wifiSsid"></div>
      <div><label>Password</label><input id="wifiPass" type="password"></div>
    </div>
    <button class="btn btn-p" onclick="connectWifi()">Connect</button>
  </div>
</div>

</main>
</div><!-- .app -->

<script>
/* ═══ Helpers ═══ */
const $=id=>document.getElementById(id);
const NAMES=['TV','Rec1','Rec2','Tuner1','Play1','Audio','Tuner2','Tuner3','Play2','Rec3','Tuner4','Rsvd','Rsvd','Rsvd','Free','Bcast'];
let allLogs=[], paused=false, autoTimer=null, statusData={};

function toast(m,ok=true){let t=document.createElement('div');t.className='toast';t.style.background=ok?'var(--ok)':'var(--hl)';t.textContent=m;document.body.appendChild(t);setTimeout(()=>t.remove(),2500)}
async function api(u,method='GET',body=null){
  try{let o={method,headers:{'Content-Type':'application/json'}};if(body)o.body=JSON.stringify(body);
  let r=await fetch(u,o),j=await r.json();
  if(j.success===false)toast(j.error||'Failed',false);
  else if(j.message)toast(j.message);
  else if(j.success)toast('OK');
  return j;}catch(e){toast('Network error',false)}
}
// physical address helper for inline onclick
function ph(i){let pa=statusData.cec_physical||0x1000;return i?((pa>>8)&0xFF):(pa&0xFF)}

/* ═══ Tabs ═══ */
document.querySelectorAll('.tabs button').forEach(b=>b.addEventListener('click',()=>{
  document.querySelectorAll('.tabs button').forEach(x=>x.classList.remove('on'));
  document.querySelectorAll('.view').forEach(x=>x.classList.remove('on'));
  b.classList.add('on');$('v-'+b.dataset.v).classList.add('on');
  if(b.dataset.v==='monitor')refreshLog();
  if(b.dataset.v==='config'){loadConfig();loadSysInfo();}
}));

/* ═══ Status polling ═══ */
async function refreshStatus(){
  let s=await api('/api/status');if(!s)return;statusData=s;
  let dot=s.wifi_status==='Connected'?'on':'warn';
  $('hdrDot').className='dot '+dot;
  $('hdrInfo').textContent=`${s.wifi_status} \u00b7 ${s.wifi_ip} \u00b7 ${s.heap_free}B free \u00b7 ${(s.uptime_ms/60000).toFixed(1)}m up \u00b7 v${s.version}`;
}
refreshStatus();setInterval(refreshStatus,5000);

/* ═══ CEC Send (used by remote + control) ═══ */
async function cecSend(dest,opts){
  let body={destination:dest,data:opts.d};
  if(opts.s!==undefined)body.source=opts.s;
  return api('/api/cec/send','POST',body);
}
function setInput(pa){api('/api/cec/input','POST',{physical_address:pa})}

/* ═══ Bus Monitor ═══ */
async function refreshLog(){
  if(paused)return;
  let entries=await api('/api/cec/log');
  if(!entries||!Array.isArray(entries))return;
  allLogs=entries;renderLog();
}
function renderLog(){
  let fDir=$('fDir').value, fSrc=$('fSrc').value.trim().toLowerCase(), fDst=$('fDst').value.trim().toLowerCase();
  let fOp=$('fOp').value.trim().toLowerCase(), fText=$('fText').value.trim().toLowerCase();
  let filtered=allLogs.filter(e=>{
    if(fDir&&e.dir!==fDir)return false;
    if(fSrc){let s=e.hex.split(':')[0];if(!s)return false;let sa=((parseInt(s,16)>>4)&0xf).toString(16);if(sa!==fSrc)return false;}
    if(fDst){let s=e.hex.split(':')[0];if(!s)return false;let da=(parseInt(s,16)&0xf).toString(16);if(da!==fDst)return false;}
    if(fOp){let parts=e.hex.split(':');if(parts.length<2||parts[1].toLowerCase()!==fOp)return false;}
    if(fText&&!e.msg.toLowerCase().includes(fText)&&!e.hex.toLowerCase().includes(fText))return false;
    return true;
  });
  let rx=allLogs.filter(x=>x.dir==='rx').length,tx=allLogs.filter(x=>x.dir==='tx').length;
  $('statTotal').textContent=allLogs.length;$('statRx').textContent=rx;$('statTx').textContent=tx;
  let html='';
  filtered.forEach(e=>{
    let cls=e.dir==='rx'?'pill-rx':'pill-tx';
    let ts=(e.t/1000).toFixed(1)+'s';
    html+=`<div class="le"><span class="ts">${ts}</span><span class="pill ${cls}">${e.dir.toUpperCase()}</span><span class="hex">${e.hex}</span><span class="msg">${esc(e.msg)}</span><button class="copy-btn" onclick="navigator.clipboard.writeText('${e.hex}');toast('Copied')">copy</button></div>`;
  });
  $('logWrap').innerHTML=html||'<div style="padding:12px;color:var(--dim);text-align:center">No messages yet. Enable promiscuous mode in Config to see all bus traffic.</div>';
  if($('autoScroll').checked)$('logWrap').scrollTop=$('logWrap').scrollHeight;
}
function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}
function togglePause(){paused=!paused;$('pauseBtn').textContent=paused?'Resume':'Pause';$('pauseBtn').classList.toggle('btn-w',paused)}
async function clearLog(){await api('/api/cec/log','DELETE');allLogs=[];renderLog()}
function clearFilters(){$('fDir').value='';$('fSrc').value='';$('fDst').value='';$('fOp').value='';$('fText').value='';renderLog()}
function exportLog(){
  let lines=allLogs.map(e=>`${(e.t/1000).toFixed(3)}\t${e.dir}\t${e.hex}\t${e.msg}`);
  let blob=new Blob(['Time\tDir\tHex\tMessage\n'+lines.join('\n')],{type:'text/tab-separated-values'});
  let a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='cec_log_'+Date.now()+'.tsv';a.click();
}
['fDir','fSrc','fDst','fOp','fText'].forEach(id=>$(id).addEventListener('input',renderLog));
autoTimer=setInterval(refreshLog,1500);

/* ═══ Builder ═══ */
function updateBuilder(){
  let op=$('bOpcode').value,ops=$('bOperands').value.trim();
  if(!op){$('bPreview').textContent='Select an opcode';return}
  let src=$('bSrc').value,dst=$('bDst').value||'0';
  let srcH=src?parseInt(src).toString(16).toUpperCase():'*';
  let dstH=parseInt(dst).toString(16).toUpperCase();
  let frame=srcH+dstH+':'+op;
  if(ops)frame+=':'+ops.split(/[\s,:]+/).filter(Boolean).join(':');
  $('bPreview').textContent=frame.toUpperCase();
}
function sendBuilt(){
  let op=$('bOpcode').value;if(!op)return toast('Select an opcode',false);
  let dst=parseInt($('bDst').value)||0;
  let bytes=[parseInt(op,16)];
  let ops=$('bOperands').value.trim();
  if(ops)ops.split(/[\s,:]+/).filter(Boolean).forEach(b=>bytes.push(parseInt(b,16)));
  let body={destination:dst,data:bytes};
  let src=$('bSrc').value;if(src!=='')body.source=parseInt(src);
  api('/api/cec/send','POST',body);
}
function sendRawHex(){
  let hex=$('rawHex').value.trim();if(!hex)return toast('Enter hex bytes',false);
  let bytes=hex.split(/[\s:,]+/).filter(Boolean).map(b=>parseInt(b,16));
  let dst=parseInt($('rawDst').value);
  api('/api/cec/send','POST',{destination:dst,data:bytes});
}

/* ═══ Device Scan ═══ */
async function scanDevices(){
  $('devMap').innerHTML='<div style="color:var(--dim);font-size:.8rem">Scanning addresses 0-14...</div>';
  let found=[];
  // query each logical address for OSD name (0x46) and power status (0x8F)
  for(let a=0;a<15;a++){
    await cecSend(a,{d:[0x46]});  // Give OSD Name
    await cecSend(a,{d:[0x8F]});  // Give Power Status
  }
  toast('Scan sent. Watch monitor for responses.');
  // After a delay, check the log for responses
  setTimeout(async()=>{
    await refreshLog();
    let chips={};
    allLogs.forEach(e=>{
      if(e.dir!=='rx')return;
      let parts=e.hex.split(':');if(parts.length<2)return;
      let hdr=parseInt(parts[0],16);
      let src=(hdr>>4)&0xf,opcode=parseInt(parts[1],16);
      if(!chips[src])chips[src]={addr:src,name:NAMES[src],osd:'',power:'?'};
      if(opcode===0x47){// Set OSD Name
        let bytes=parts.slice(2).map(b=>parseInt(b,16));
        chips[src].osd=bytes.map(b=>String.fromCharCode(b)).join('');
      }
      if(opcode===0x90){// Report Power Status
        let ps=parseInt(parts[2],16);
        chips[src].power=['On','Standby','To On','To Standby'][ps]||'?';
      }
    });
    let html='';
    Object.values(chips).sort((a,b)=>a.addr-b.addr).forEach(d=>{
      html+=`<div class="dev-chip"><div class="da">0x${d.addr.toString(16).toUpperCase()} - ${d.name}</div><div class="dn">${d.osd||'(no name)'}</div><div class="dp">Power: ${d.power}</div></div>`;
    });
    $('devMap').innerHTML=html||'<div style="color:var(--dim);font-size:.8rem">No devices responded. Make sure promiscuous mode is enabled.</div>';
  },4000);
}

/* ═══ Config ═══ */
async function loadConfig(){
  let c=await api('/api/config');if(!c)return;
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
  api('/api/config','POST',{
    cec_pin:parseInt($('cfgPin').value),
    cec_address:parseInt($('cfgAddr').value),
    cec_physical:parseInt($('cfgPhys').value),
    cec_osd_name:$('cfgOsd').value,
    cec_promiscuous:$('cfgPromisc').checked,
    cec_monitor_mode:$('cfgMonitor').checked,
    hostname:$('cfgHostname').value,
    log_buffer_size:parseInt($('cfgLogBuf').value)
  });
}
function resetConfig(){if(confirm('Factory reset? All settings will be erased.'))api('/api/system/reset','POST')}
function restartDevice(){if(confirm('Restart device?'))api('/api/system/restart','POST')}
async function loadSysInfo(){
  let s=await api('/api/status');if(!s)return;
  $('sysInfo').innerHTML=`Version: ${s.version}<br>WiFi: ${s.wifi_status} (${s.wifi_ip})<br>Heap: ${s.heap_free} bytes free<br>Uptime: ${(s.uptime_ms/60000).toFixed(1)} min<br>CEC Addr: 0x${s.cec_address.toString(16).toUpperCase()}<br>CEC Phys: 0x${s.cec_physical.toString(16).toUpperCase().padStart(4,'0')}<br>OSD: ${s.cec_osd_name}`;
}

/* ═══ WiFi ═══ */
async function scanWifi(){
  $('wifiList').innerHTML='<div style="padding:12px;color:var(--dim)">Scanning...</div>';
  let nets=await api('/api/wifi/scan');
  if(!nets||!Array.isArray(nets)){$('wifiList').innerHTML='<div style="padding:12px;color:var(--dim)">Scan failed</div>';return}
  nets.sort((a,b)=>b.rssi-a.rssi);
  let html='';
  nets.forEach(n=>{
    let bars=n.rssi>-50?'\u2589\u2589\u2589\u2589':n.rssi>-65?'\u2589\u2589\u2589':n.rssi>-75?'\u2589\u2589':'\u2589';
    let lock=n.encrypted?'\uD83D\uDD12 ':'';
    html+=`<div class="wnet" onclick="$('wifiSsid').value='${n.ssid}'">${lock}<strong>${n.ssid}</strong><span class="ws">${n.rssi}dBm ${bars}</span></div>`;
  });
  $('wifiList').innerHTML=html||'<div style="padding:12px;color:var(--dim)">No networks found.</div>';
}
function connectWifi(){
  let s=$('wifiSsid').value,p=$('wifiPass').value;
  if(!s)return toast('Enter SSID',false);
  api('/api/wifi/connect','POST',{ssid:s,password:p});
}
</script>
</body>
</html>
)rawliteral";
