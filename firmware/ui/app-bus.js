/* ── Bus monitor ─────────────────────────────────────────────────────────── */

async function refreshLog() {
  if (state.paused) return;
  var entries = await api('/api/cec/log');
  if (!entries || !Array.isArray(entries)) return;
  state.logs = entries;
  renderLog();
  renderRecentEvents();
}

function renderLog() {
  var dir = $('filterDir').value;
  var src = $('filterSrc').value.trim().toLowerCase();
  var dst = $('filterDst').value.trim().toLowerCase();
  var op  = $('filterOp').value.trim().toLowerCase();
  var txt = $('filterTxt').value.trim().toLowerCase();

  var list = state.logs.filter(function (e) {
    if (dir && e.dir !== dir) return false;
    var parts  = e.hex.split(':');
    var header = parseInt(parts[0], 16);
    var esrc   = ((header >> 4) & 0xF).toString(16);
    var edst   = (header & 0xF).toString(16);
    var eop    = parts[1] ? parts[1].toLowerCase() : '';
    if (src && esrc !== src) return false;
    if (dst && edst !== dst) return false;
    if (op  && eop  !== op)  return false;
    if (txt && !e.hex.toLowerCase().includes(txt) && !e.msg.toLowerCase().includes(txt)) return false;
    return true;
  });

  $('logTotal').textContent = state.logs.length;
  $('logRx').textContent    = state.logs.filter(function (e) { return e.dir === 'rx'; }).length;
  $('logTx').textContent    = state.logs.filter(function (e) { return e.dir === 'tx'; }).length;

  if (!list.length) {
    $('logList').innerHTML = '<div class="empty">No entries match filters.</div>';
    return;
  }

  $('logList').innerHTML = list.map(function (e) {
    return '<div class="log-entry">' +
      '<span class="log-t">'   + fmtMs(e.t) + '</span>' +
      '<span class="log-dir '  + e.dir + '">' + e.dir.toUpperCase() + '</span>' +
      '<code class="log-hex">' + esc(e.hex)  + '</code>' +
      '<span class="log-msg">' + esc(e.msg)  + '</span>' +
      '<button class="log-copy" data-h="' + escAttr(e.hex) + '">\u2398</button>' +
      '</div>';
  }).join('');

  $('logList').querySelectorAll('.log-copy').forEach(function (btn) {
    btn.addEventListener('click', function () {
      navigator.clipboard.writeText(btn.dataset.h);
      toast('Copied', 'ok');
    });
  });

  if ($('autoScroll').checked) $('logList').scrollTop = $('logList').scrollHeight;
}

function renderRecentEvents() {
  var latest = state.logs.slice(-8).reverse();
  if (!latest.length) {
    $('recentEvents').innerHTML = '<div class="empty">No events yet.</div>';
    return;
  }
  $('recentEvents').innerHTML = '<div class="log-list" style="max-height:180px">' +
    latest.map(function (e) {
      return '<div class="log-entry">' +
        '<span class="log-t">'   + fmtMs(e.t)   + '</span>' +
        '<span class="log-dir '  + e.dir + '">'  + e.dir.toUpperCase() + '</span>' +
        '<code class="log-hex">' + esc(e.hex)    + '</code>' +
        '<span class="log-msg">' + esc(e.msg)    + '</span>' +
        '<span></span></div>';
    }).join('') + '</div>';
}

/* ── Frame builder ───────────────────────────────────────────────────────── */
function bindFrameBuilder() {
  ['txSrc', 'txDst', 'txOpcode', 'txOperands'].forEach(function (id) {
    $(id).addEventListener('input',  updatePreview);
    $(id).addEventListener('change', updatePreview);
  });
}

function updatePreview() {
  var src = $('txSrc').value;
  var dst = $('txDst').value || '0';
  var op  = $('txOpcode').value;
  if (!op) { $('txPreview').textContent = '\u2014'; return; }
  var sHex  = src === '' ? '*' : Number(src).toString(16).toUpperCase();
  var dHex  = Number(dst).toString(16).toUpperCase();
  var ops   = $('txOperands').value.trim();
  var extra = ops
    ? ':' + ops.split(/[\s,:]+/).filter(Boolean).map(function (v) { return v.toUpperCase(); }).join(':')
    : '';
  $('txPreview').textContent = sHex + dHex + ':' + op.toUpperCase() + extra;
}

async function sendFrameCmd() {
  var op = $('txOpcode').value;
  if (!op) { toast('Select an opcode', 'err'); return; }
  var bytes = [parseInt(op, 16)];
  var ops = $('txOperands').value.trim();
  if (ops) ops.split(/[\s,:]+/).filter(Boolean).forEach(function (v) { bytes.push(parseInt(v, 16)); });
  var src = $('txSrc').value;
  var dst = parseInt($('txDst').value, 10);
  await apiCec(dst, bytes, src === '' ? undefined : src);
  await refreshLog();
}

/* ── Log filters ─────────────────────────────────────────────────────────── */
function bindFilters() {
  ['filterDir', 'filterSrc', 'filterDst', 'filterOp', 'filterTxt'].forEach(function (id) {
    $(id).addEventListener('input', renderLog);
  });
}
