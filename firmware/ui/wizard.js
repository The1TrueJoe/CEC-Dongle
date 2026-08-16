(function () {
  var selectedSsid = '';
  var selectedOpen = false;

  /* ── Helpers ──────────────────────────────────────────────────────── */
  function esc(s) {
    return String(s)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function escAttr(s) { return esc(s); }

  function el(id) { return document.getElementById(id); }

  // fetch() has no default timeout — on an ESP8266 softAP a hung request can
  // sit forever (the chip briefly drops AP service while it scans, and a
  // dead TCP connection has no browser-visible signal until a long OS-level
  // timeout). Force a short one so failures show up as failures.
  function fetchTimeout(url, opts, ms) {
    var ctrl = new AbortController();
    var timer = setTimeout(function () { ctrl.abort(); }, ms || 4000);
    opts = opts || {};
    opts.signal = ctrl.signal;
    return fetch(url, opts).then(
      function (r) { clearTimeout(timer); return r; },
      function (e) { clearTimeout(timer); throw e; }
    );
  }

  function show(cardId) {
    ['scanCard', 'credCard', 'connectingCard', 'doneCard'].forEach(function (id) {
      el(id).classList[id === cardId ? 'remove' : 'add']('hidden');
    });
  }

  // Inline Lucide icon paths (https://lucide.dev, ISC license) — no CDN, this
  // page is served from an isolated AP with no upstream internet.
  var ICON_PATHS = {
    lock:      '<rect width="18" height="11" x="3" y="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/>',
    lockOpen:  '<rect width="18" height="11" x="3" y="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 9.9-1"/>',
    eye:       '<path d="M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0"/><circle cx="12" cy="12" r="3"/>',
    eyeOff:    '<path d="M10.733 5.076a10.744 10.744 0 0 1 11.205 6.575 1 1 0 0 1 0 .696 10.747 10.747 0 0 1-1.444 2.49"/><path d="M14.084 14.158a3 3 0 0 1-4.242-4.242"/><path d="M17.479 17.499a10.75 10.75 0 0 1-15.417-5.151 1 1 0 0 1 0-.696 10.75 10.75 0 0 1 4.446-5.143"/><path d="m2 2 20 20"/>',
    signal4:   '<path d="M2 20h.01"/><path d="M7 20v-4"/><path d="M12 20v-8"/><path d="M17 20V8"/>',
    signal3:   '<path d="M2 20h.01"/><path d="M7 20v-4"/><path d="M12 20v-8"/>',
    signal2:   '<path d="M2 20h.01"/><path d="M7 20v-4"/>',
    signal1:   '<path d="M2 20h.01"/>',
    check:     '<circle cx="12" cy="12" r="10"/><path d="m9 12 2 2 4-4"/>',
    alert:     '<circle cx="12" cy="12" r="10"/><line x1="12" x2="12" y1="8" y2="12"/><line x1="12" x2="12.01" y1="16" y2="16"/>'
  };

  function icon(name, extraClass) {
    return '<svg class="icon' + (extraClass ? ' ' + extraClass : '') +
      '" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" ' +
      'stroke-linecap="round" stroke-linejoin="round">' + ICON_PATHS[name] + '</svg>';
  }

  function rssiBar(rssi) {
    if (rssi >= -55) return icon('signal4');
    if (rssi >= -70) return icon('signal3');
    if (rssi >= -80) return icon('signal2');
    return icon('signal1');
  }

  /* ── Scan ─────────────────────────────────────────────────────────── */
  // scanGen lets a new scan cancel a stale in-flight poll.
  var scanGen = 0;

  // The ESP8266 has a single 2.4GHz radio: scanning while its softAP is up
  // to serve this very page briefly stalls the AP, and the page's own
  // request can fail or hang mid-scan. That's transient, so retry a few
  // times automatically before asking the user to do it manually.
  var MAX_SCAN_RETRIES = 5;

  function showRetryMsg(text, gen) {
    if (gen !== scanGen) return;
    el('netList').innerHTML = '<div class="msg">' + text + ' <button class="btn-link" id="retryBtn">Retry</button></div>';
    el('retryBtn').addEventListener('click', scan);
  }

  function scan() {
    scanGen++;
    var gen = scanGen;
    var attempt = 0;
    el('netList').innerHTML = '<div class="msg"><div class="spinner-sm"></div>Scanning&#8230;</div>';
    (function poll() {
      fetchTimeout('/api/wifi/scan', {}, 4000)
        .then(function (r) { return r.json(); })
        .then(function (data) {
          if (gen !== scanGen) return; // superseded by a newer scan request
          if (data.scanning) { setTimeout(poll, 1000); return; }
          var nets = data.networks || [];
          if (!nets.length) {
            if (attempt++ < MAX_SCAN_RETRIES) { setTimeout(poll, 1200); return; }
            showRetryMsg('No networks found.', gen);
            return;
          }
          nets.sort(function (a, b) { return b.rssi - a.rssi; });
          el('netList').innerHTML = nets.map(function (n) {
            return '<div class="net-item" data-ssid="' + escAttr(n.ssid) +
              '" data-open="' + (!n.encrypted) + '">' +
              '<span class="net-ssid">' + esc(n.ssid) + '</span>' +
              '<span class="net-lock' + (n.encrypted ? '' : ' open') + '">' + icon(n.encrypted ? 'lock' : 'lockOpen') + '</span>' +
              '<span class="net-bars">' + rssiBar(n.rssi) + '</span>' +
              '</div>';
          }).join('');
          el('netList').querySelectorAll('.net-item').forEach(function (row) {
            row.addEventListener('click', function () {
              selectNetwork(row.dataset.ssid, row.dataset.open === 'true');
            });
          });
        })
        .catch(function () {
          if (gen !== scanGen) return;
          if (attempt++ < MAX_SCAN_RETRIES) { setTimeout(poll, 1200); return; }
          showRetryMsg('Scan failed.', gen);
        });
    }());
  }

  /* ── Select network → credentials screen ─────────────────────────── */
  function selectNetwork(ssid, open) {
    selectedSsid = ssid;
    selectedOpen = open;
    el('credHeading').textContent = ssid;
    el('wifiPass').value = '';
    el('credError').classList.add('hidden');

    if (open) {
      el('openBadge').classList.remove('hidden');
      el('passWrap').classList.add('hidden');
    } else {
      el('openBadge').classList.add('hidden');
      el('passWrap').classList.remove('hidden');
    }

    show('credCard');
    if (!open) setTimeout(function () { el('wifiPass').focus(); }, 120);
  }

  /* ── Connect ──────────────────────────────────────────────────────── */
  function connect() {
    var pass = el('wifiPass').value;
    var hostname = el('hostname').value.trim().toLowerCase().replace(/[^a-z0-9-]/g, '');

    if (!selectedOpen && !pass) {
      el('wifiPass').focus();
      return;
    }

    el('connectBtn').disabled = true;
    el('credError').classList.add('hidden');
    show('connectingCard');
    el('connectMsg').textContent = 'Connecting to ' + selectedSsid + '\u2026';

    function doConnect() {
      fetchTimeout('/api/wifi/connect', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid: selectedSsid, password: pass })
      }, 4000)
        .then(function () {
          el('connectMsg').textContent = 'Joining \u201c' + selectedSsid + '\u201d\u2026';
          setTimeout(function () { pollJoin(0); }, 1500);
        })
        .catch(function () {
          fail('Could not reach the device to start the connection. Try again.');
        });
    }

    function pollJoin(attempt) {
      fetchTimeout('/api/status', {}, 1500)
        .then(function (r) { return r.json(); })
        .then(function (status) {
          if (status.wifi_status === 'Connected') { succeed(status); return; }
          if (attempt < MAX_POLL_ATTEMPTS) { setTimeout(function () { pollJoin(attempt + 1); }, POLL_INTERVAL_MS); return; }
          // Still reachable on the setup network and never reported Connected.
          fail('Could not join \u201c' + selectedSsid + '\u201d. Check the password and try again.');
        })
        .catch(function () { ambiguous(); });
    }

    function succeed(status) {
      var h = el('hostname').value.trim().toLowerCase().replace(/[^a-z0-9-]/g, '') || 'cec-dongle';
      el('doneIcon').className = 'done-icon';
      el('doneIcon').innerHTML = icon('check', 'icon-lg');
      el('doneTitle').textContent = 'Connected!';
      el('doneMsg').textContent =
        'Joined \u201c' + selectedSsid + '\u201d' + (status.wifi_ip ? ' at ' + status.wifi_ip : '') + '.\n\n' +
        'Reconnect your device to \u201c' + selectedSsid + '\u201d, then visit\n' +
        'http://' + h + '.local';
      show('doneCard');
    }

    function ambiguous() {
      var h = el('hostname').value.trim().toLowerCase().replace(/[^a-z0-9-]/g, '') || 'cec-dongle';
      el('doneIcon').className = 'done-icon warn';
      el('doneIcon').innerHTML = icon('alert', 'icon-lg');
      el('doneTitle').textContent = 'Setup network disappeared';
      el('doneMsg').textContent =
        'That usually means it joined \u201c' + selectedSsid + '\u201d and shut down its own network \u2014 ' +
        'but this page has no way to confirm it from here.\n\n' +
        'Reconnect your device to \u201c' + selectedSsid + '\u201d, then visit\n' +
        'http://' + h + '.local\n\n' +
        'If a network named \u201cCEC-Dongle-\u2026\u201d is still visible after a minute, the connection failed \u2014 ' +
        'reconnect to it and try again.';
      show('doneCard');
    }

    function fail(message) {
      el('connectBtn').disabled = false;
      el('credError').innerHTML = icon('alert') + ' <span>' + esc(message) + '</span>';
      el('credError').classList.remove('hidden');
      show('credCard');
    }

    if (hostname) {
      fetchTimeout('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ hostname: hostname })
      }, 3000).then(doConnect).catch(doConnect);
    } else {
      doConnect();
    }
  }

  /* ── Show/hide password toggle ─────────────────────────────────────
     Initial "eye" glyph is already in the HTML (matches the default
     type="password" state); JS only swaps it on toggle. */
  el('showPass').addEventListener('click', function () {
    var inp = el('wifiPass');
    var showing = inp.type === 'text';
    inp.type = showing ? 'password' : 'text';
    el('showPass').innerHTML = icon(showing ? 'eye' : 'eyeOff');
  });

  /* ── Button bindings ──────────────────────────────────────────────── */
  el('rescanBtn').addEventListener('click', scan);
  el('backBtn').addEventListener('click', function () { show('scanCard'); });
  el('connectBtn').addEventListener('click', connect);
  el('wifiPass').addEventListener('keydown', function (e) {
    if (e.key === 'Enter') connect();
  });
  el('hostname').addEventListener('keydown', function (e) {
    if (e.key === 'Enter') connect();
  });

  /* ── Init ─────────────────────────────────────────────────────────── */
  scan();
})();
