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

  function show(cardId) {
    ['scanCard', 'credCard', 'connectingCard', 'doneCard'].forEach(function (id) {
      el(id).classList[id === cardId ? 'remove' : 'add']('hidden');
    });
  }

  function rssiBar(rssi) {
    if (rssi >= -55) return '&#9602;&#9604;&#9606;&#9608;';
    if (rssi >= -70) return '&#9602;&#9604;&#9606;&#9617;';
    if (rssi >= -80) return '&#9602;&#9604;&#9617;&#9617;';
    return '&#9602;&#9617;&#9617;&#9617;';
  }

  /* ── Scan ─────────────────────────────────────────────────────────── */
  function scan() {
    el('netList').innerHTML = '<div class="msg"><div class="spinner-sm"></div>Scanning&#8230;</div>';
    fetch('/api/wifi/scan')
      .then(function (r) { return r.json(); })
      .then(function (nets) {
        if (!Array.isArray(nets) || !nets.length) {
          el('netList').innerHTML =
            '<div class="msg">No networks found. <button class="btn-link" id="retryBtn">Retry</button></div>';
          el('retryBtn').addEventListener('click', scan);
          return;
        }
        nets.sort(function (a, b) { return b.rssi - a.rssi; });
        el('netList').innerHTML = nets.map(function (n) {
          return '<div class="net-item" data-ssid="' + escAttr(n.ssid) +
            '" data-open="' + (!n.encrypted) + '">' +
            '<span class="net-ssid">' + esc(n.ssid) + '</span>' +
            (n.encrypted ? '<span class="net-lock">&#128274;</span>' : '<span class="net-lock" style="color:var(--success)">&#128275;</span>') +
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
        el('netList').innerHTML =
          '<div class="msg">Scan failed. <button class="btn-link" id="retryBtn">Retry</button></div>';
        el('retryBtn').addEventListener('click', scan);
      });
  }

  /* ── Select network → credentials screen ─────────────────────────── */
  function selectNetwork(ssid, open) {
    selectedSsid = ssid;
    selectedOpen = open;
    el('credHeading').textContent = ssid;
    el('wifiPass').value = '';

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
    show('connectingCard');
    el('connectMsg').textContent = 'Connecting to ' + selectedSsid + '\u2026';

    // Optionally save hostname first, then connect
    function doConnect() {
      fetch('/api/wifi/connect', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid: selectedSsid, password: pass })
      })
        .then(function () {
          el('connectMsg').textContent = 'Joining network\u2026';
          // AP will shut down, we can't poll. Show done screen after brief delay.
          setTimeout(showDone, 2500);
        })
        .catch(function () {
          el('connectBtn').disabled = false;
          show('credCard');
        });
    }

    if (hostname) {
      fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ hostname: hostname })
      }).then(doConnect).catch(doConnect);
    } else {
      doConnect();
    }
  }

  function showDone() {
    var h = el('hostname').value.trim().toLowerCase().replace(/[^a-z0-9-]/g, '') || 'cec-dongle';
    el('doneMsg').textContent =
      'The device is joining \u201c' + selectedSsid + '\u201d.\n\n' +
      'Reconnect your device to \u201c' + selectedSsid + '\u201d, then visit\n' +
      'http://' + h + '.local\n' +
      'or check your router\u2019s device list for the IP address.';
    show('doneCard');
  }

  /* ── Show/hide password toggle ────────────────────────────────────── */
  el('showPass').addEventListener('click', function () {
    var inp = el('wifiPass');
    inp.type = inp.type === 'password' ? 'text' : 'password';
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
