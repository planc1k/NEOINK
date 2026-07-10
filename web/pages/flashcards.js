const DECK_DIR = '/.crosspoint/flashcards/decks';
let currentDecks = [];
let selectedDeckPath = '';

function setStatus(message, kind) {
  const el = document.getElementById('status');
  el.className = kind ? 'status-' + kind : '';
  el.style.display = message ? 'block' : 'none';
  el.textContent = message || '';
}

function escapeHtml(value) {
  return String(value)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#039;');
}

function sanitizeDeckName(name) {
  const base = name.replace(/\.[^.]+$/, '');
  const clean = base.replace(/[^A-Za-z0-9_-]+/g, '_').replace(/^_+|_+$/g, '');
  return clean || 'flashcards';
}

function tsvEscape(value) {
  return String(value).replace(/\r/g, '').replace(/\t/g, ' ').replace(/\n{3,}/g, '\n\n').trim();
}

function stripHtml(value) {
  const text = String(value)
    .replace(/<\s*br\s*\/?>/gi, '\n')
    .replace(/<\/\s*(div|p|li|tr|h[1-6])\s*>/gi, '\n')
    .replace(/<[^>]+>/g, ' ')
    .replace(/&nbsp;/g, ' ')
    .replace(/&amp;/g, '&')
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'");
  return text.replace(/[ \f\v]+/g, ' ').replace(/\n\s+/g, '\n').trim();
}

async function loadDecks() {
  const el = document.getElementById('deckList');
  try {
    const res = await fetch('/api/flashcards/decks?_=' + Date.now());
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const decks = await res.json();
    currentDecks = decks;
    if (!selectedDeckPath && decks.length) selectedDeckPath = decks[0].path;
    if (!decks.length) {
      el.innerHTML = '<p class="empty">No decks installed</p>';
      document.getElementById('deckDetail').innerHTML = '<p class="empty">Select a deck</p>';
      return;
    }
    el.innerHTML = decks.map(deck => {
      if (!deck.valid) {
        return `<div class="deck ${deck.path === selectedDeckPath ? 'selected' : ''}" data-path="${escapeHtml(deck.path)}"><div><div class="deck-title">${escapeHtml(deck.title)}</div><div class="deck-meta">${escapeHtml(deck.error || 'Invalid deck')}</div></div></div>`;
      }
      const retention = deck.totalReviews ? Math.round((deck.retentionPermille || 0) / 10) + '%' : '-';
      return `<div class="deck ${deck.path === selectedDeckPath ? 'selected' : ''}">
        <div>
          <button class="mini-btn" data-action="select" data-path="${escapeHtml(deck.path)}">${escapeHtml(deck.title)}</button>
          <div class="deck-meta">${escapeHtml(deck.path)}</div>
        </div>
        <div class="stats">
          <span class="pill">${deck.totalCards} cards</span>
          <span class="pill due">${deck.dueCards} due</span>
          <span class="pill new">${deck.newCards} new</span>
          <span class="pill">${retention}</span>
        </div>
        <div class="deck-actions">
          <a class="mini-btn" href="/api/flashcards/export?path=${encodeURIComponent(deck.path)}">Export</a>
          <button class="mini-btn" data-action="rename" data-path="${escapeHtml(deck.path)}">Rename</button>
          <button class="mini-btn" data-action="reset" data-path="${escapeHtml(deck.path)}">Reset</button>
          <button class="mini-btn danger" data-action="delete" data-path="${escapeHtml(deck.path)}">Delete</button>
        </div>
      </div>`;
    }).join('');
    if (selectedDeckPath && decks.some(deck => deck.path === selectedDeckPath)) {
      await loadDeckDetail(selectedDeckPath);
    } else {
      selectedDeckPath = decks[0].path;
      await loadDeckDetail(selectedDeckPath);
    }
  } catch (err) {
    el.innerHTML = '<p class="empty">Failed to load decks</p>';
  }
}

function pct(value, total) {
  if (!total) return 0;
  return Math.max(0, Math.min(100, Math.round((value * 100) / total)));
}

async function loadDeckDetail(path) {
  const el = document.getElementById('deckDetail');
  el.innerHTML = '<p class="empty">Loading...</p>';
  try {
    const res = await fetch('/api/flashcards/deck?path=' + encodeURIComponent(path) + '&_=' + Date.now());
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const data = await res.json();
    const s = data.summary;
    if (!s.valid) {
      el.innerHTML = `<p class="empty">${escapeHtml(s.error || 'Invalid deck')}</p>`;
      return;
    }
    const retention = s.totalReviews ? Math.round((s.retentionPermille || 0) / 10) + '%' : '-';
    const reviewedPct = pct(s.reviewedCards, s.totalCards);
    const rows = (data.cards || []).slice(0, 8).map(card => `<tr>
      <td>${escapeHtml(card.front || '')}</td>
      <td>${card.reviewCount || 0}</td>
      <td>${card.intervalSessions || 0}</td>
      <td>${card.lapseCount || 0}</td>
    </tr>`).join('');
    el.innerHTML = `
      <div class="deck-title">${escapeHtml(s.title)}</div>
      <div class="deck-meta">${escapeHtml(s.path)}</div>
      <div class="bar"><span style="width:${reviewedPct}%"></span></div>
      <div class="metric-grid">
        <div class="metric"><div class="metric-value">${s.totalCards}</div><div class="metric-label">Cards</div></div>
        <div class="metric"><div class="metric-value">${s.dueCards}</div><div class="metric-label">Due</div></div>
        <div class="metric"><div class="metric-value">${s.reviewedCards}</div><div class="metric-label">Reviewed</div></div>
        <div class="metric"><div class="metric-value">${retention}</div><div class="metric-label">Retention</div></div>
        <div class="metric"><div class="metric-value">${s.totalReviews}</div><div class="metric-label">Total reviews</div></div>
        <div class="metric"><div class="metric-value">${s.totalLapses}</div><div class="metric-label">Lapses</div></div>
        <div class="metric"><div class="metric-value">${s.totalSessions}</div><div class="metric-label">Sessions</div></div>
        <div class="metric"><div class="metric-value">${s.matureCards}</div><div class="metric-label">Mature</div></div>
      </div>
      <div class="stats" style="justify-content:flex-start;margin-top:12px">
        <span class="pill due">Again ${s.totalAgain}</span>
        <span class="pill">Hard ${s.totalHard}</span>
        <span class="pill">Good ${s.totalGood}</span>
        <span class="pill new">Easy ${s.totalEasy}</span>
      </div>
      <table class="card-table">
        <tr><th>Front</th><th>Reviews</th><th>Interval</th><th>Lapses</th></tr>
        ${rows || '<tr><td colspan="4">No card progress yet</td></tr>'}
      </table>`;
  } catch (err) {
    el.innerHTML = '<p class="empty">Failed to load deck stats</p>';
  }
}

async function apiPost(url, body) {
  const res = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  const text = await res.text();
  let data = {};
  try { data = text ? JSON.parse(text) : {}; } catch (e) { data = { error: text }; }
  if (!res.ok || data.error) throw new Error(data.error || 'Request failed');
  return data;
}

document.getElementById('deckList').addEventListener('click', async event => {
  const target = event.target.closest('[data-action]');
  if (!target) return;
  const action = target.dataset.action;
  const path = target.dataset.path;
  if (!path) return;

  try {
    if (action === 'select') {
      selectedDeckPath = path;
      await loadDeckDetail(path);
      return;
    }
    if (action === 'reset') {
      if (!confirm('Reset progress for this deck?')) return;
      await apiPost('/api/flashcards/reset', { path });
      setStatus('Progress reset', 'ok');
    } else if (action === 'delete') {
      if (!confirm('Delete this deck and its progress?')) return;
      await apiPost('/api/flashcards/delete', { path, deleteProgress: true });
      if (selectedDeckPath === path) selectedDeckPath = '';
      setStatus('Deck deleted', 'ok');
    } else if (action === 'rename') {
      const deck = currentDecks.find(d => d.path === path);
      const name = prompt('New deck name', deck ? deck.title : '');
      if (!name) return;
      const data = await apiPost('/api/flashcards/rename', { path, name });
      selectedDeckPath = data.path || '';
      setStatus('Deck renamed', 'ok');
    }
    await loadDecks();
  } catch (err) {
    setStatus(err.message, 'err');
  }
});

function readVarint(bytes, offset) {
  let value = 0;
  let i = 0;
  for (; i < 8; i++) {
    const b = bytes[offset + i];
    value = (value * 128) + (b & 0x7f);
    if ((b & 0x80) === 0) return { value, length: i + 1 };
  }
  value = (value * 256) + bytes[offset + 8];
  return { value, length: 9 };
}

function be16(bytes, offset) {
  return (bytes[offset] << 8) | bytes[offset + 1];
}

function be32(bytes, offset) {
  return ((bytes[offset] * 0x1000000) + ((bytes[offset + 1] << 16) | (bytes[offset + 2] << 8) | bytes[offset + 3])) >>> 0;
}

function readSigned(bytes, offset, length) {
  let value = 0;
  for (let i = 0; i < length; i++) value = (value * 256) + bytes[offset + i];
  const sign = 2 ** ((length * 8) - 1);
  return value >= sign ? value - (sign * 2) : value;
}

class SQLiteLite {
  constructor(bytes) {
    this.bytes = bytes;
    this.pageSize = be16(bytes, 16);
    if (this.pageSize === 1) this.pageSize = 65536;
    if (String.fromCharCode(...bytes.slice(0, 15)) !== 'SQLite format 3') {
      throw new Error('APKG does not contain a readable SQLite collection');
    }
    this.decoder = new TextDecoder('utf-8');
  }

  pageOffset(pageNo) {
    return (pageNo - 1) * this.pageSize;
  }

  readPayload(pageNo, cellOffset, payloadLength, payloadStart) {
    const maxLocal = this.pageSize - 35;
    const minLocal = Math.floor(((this.pageSize - 12) * 32) / 255) - 23;
    let local = payloadLength;
    if (payloadLength > maxLocal) {
      local = minLocal + ((payloadLength - minLocal) % (this.pageSize - 4));
      if (local > maxLocal) local = minLocal;
    }
    const out = new Uint8Array(payloadLength);
    out.set(this.bytes.slice(payloadStart, payloadStart + local), 0);
    let copied = local;
    let overflowPage = payloadLength > local ? be32(this.bytes, payloadStart + local) : 0;
    while (overflowPage && copied < payloadLength) {
      const off = this.pageOffset(overflowPage);
      overflowPage = be32(this.bytes, off);
      const chunk = Math.min(this.pageSize - 4, payloadLength - copied);
      out.set(this.bytes.slice(off + 4, off + 4 + chunk), copied);
      copied += chunk;
    }
    return out;
  }

  parseRecord(payload) {
    const header = readVarint(payload, 0);
    const headerSize = header.value;
    let pos = header.length;
    const serials = [];
    while (pos < headerSize) {
      const v = readVarint(payload, pos);
      serials.push(v.value);
      pos += v.length;
    }
    let body = headerSize;
    return serials.map(type => {
      if (type === 0) return null;
      if (type === 8) return 0;
      if (type === 9) return 1;
      if (type >= 1 && type <= 6) {
        const len = [0, 1, 2, 3, 4, 6, 8][type];
        const n = readSigned(payload, body, len);
        body += len;
        return n;
      }
      if (type === 7) {
        body += 8;
        return null;
      }
      const len = Math.floor((type - 12) / 2);
      const data = payload.slice(body, body + len);
      body += len;
      return type % 2 ? this.decoder.decode(data) : data;
    });
  }

  readTable(rootPage) {
    const rows = [];
    const visit = pageNo => {
      const base = this.pageOffset(pageNo);
      const hdr = pageNo === 1 ? base + 100 : base;
      const type = this.bytes[hdr];
      const cellCount = be16(this.bytes, hdr + 3);
      const ptrBase = hdr + (type === 0x05 ? 12 : 8);

      if (type === 0x05) {
        for (let i = 0; i < cellCount; i++) {
          const cell = base + be16(this.bytes, ptrBase + i * 2);
          visit(be32(this.bytes, cell));
        }
        visit(be32(this.bytes, hdr + 8));
        return;
      }
      if (type !== 0x0d) throw new Error('Unsupported SQLite page type');

      for (let i = 0; i < cellCount; i++) {
        const cell = base + be16(this.bytes, ptrBase + i * 2);
        const payloadLen = readVarint(this.bytes, cell);
        const rowId = readVarint(this.bytes, cell + payloadLen.length);
        const payloadStart = cell + payloadLen.length + rowId.length;
        const payload = this.readPayload(pageNo, cell, payloadLen.value, payloadStart);
        rows.push(this.parseRecord(payload));
      }
    };
    visit(rootPage);
    return rows;
  }

  findTableRoot(name) {
    const schemaRows = this.readTable(1);
    for (const row of schemaRows) {
      if (row[0] === 'table' && row[1] === name) return Number(row[3]);
    }
    return 0;
  }
}

async function convertApkgToTsv(file, limit) {
  const zip = await JSZip.loadAsync(file);
  const collectionName = ['collection.anki21', 'collection.anki2'].find(name => zip.files[name]);
  if (!collectionName) throw new Error('No Anki collection found in APKG');
  const bytes = new Uint8Array(await zip.files[collectionName].async('arraybuffer'));
  const db = new SQLiteLite(bytes);
  const notesRoot = db.findTableRoot('notes');
  if (!notesRoot) throw new Error('No notes table found in APKG');

  const rows = db.readTable(notesRoot);
  const cards = [];
  for (const row of rows) {
    const fields = String(row[6] || '').split('\x1f').map(stripHtml);
    if (fields.length < 2 || !fields[0] || !fields[1]) continue;
    cards.push([fields[0], fields[1]]);
    if (cards.length >= limit) break;
  }
  if (!cards.length) throw new Error('No Basic-style front/back notes found');
  return 'front\tback\n' + cards.map(([front, back]) => `${tsvEscape(front)}\t${tsvEscape(back)}`).join('\n') + '\n';
}

async function uploadDeck(blob, filename) {
  const form = new FormData();
  form.append('file', blob, filename);
  const res = await fetch('/upload?path=' + encodeURIComponent(DECK_DIR), {
    method: 'POST',
    body: form
  });
  const text = await res.text();
  if (!res.ok) throw new Error(text || 'Upload failed');
}

async function handleImport(event) {
  event.preventDefault();
  const file = document.getElementById('deckFile').files[0];
  if (!file) return;
  const limit = Math.max(1, Math.min(300, parseInt(document.getElementById('cardLimit').value || '300', 10)));
  const deckBase = sanitizeDeckName(document.getElementById('deckName').value || file.name);
  const lower = file.name.toLowerCase();

  try {
    setStatus('Preparing deck...', 'info');
    if (lower.endsWith('.csv') || lower.endsWith('.tsv')) {
      const ext = lower.endsWith('.csv') ? '.csv' : '.tsv';
      await uploadDeck(file, deckBase + ext);
      setStatus('Uploaded ' + deckBase + ext, 'ok');
    } else if (lower.endsWith('.apkg')) {
      setStatus('Converting APKG...', 'info');
      const tsv = await convertApkgToTsv(file, limit);
      const blob = new Blob([tsv], { type: 'text/tab-separated-values' });
      await uploadDeck(blob, deckBase + '.tsv');
      setStatus('Converted and uploaded ' + deckBase + '.tsv', 'ok');
    } else {
      throw new Error('Choose a CSV, TSV, or APKG file');
    }
    document.getElementById('importForm').reset();
    await loadDecks();
  } catch (err) {
    setStatus(err.message, 'err');
  }
}

document.getElementById('importForm').addEventListener('submit', handleImport);
document.getElementById('progressImportForm').addEventListener('submit', async event => {
  event.preventDefault();
  const file = document.getElementById('progressFile').files[0];
  if (!file) return;
  try {
    setStatus('Importing progress...', 'info');
    const json = JSON.parse(await file.text());
    const result = await apiPost('/api/flashcards/import', json);
    setStatus(`Merged ${result.mergedRecords || 0} records across ${result.mergedDecks || 0} deck(s)`, 'ok');
    event.target.reset();
    await loadDecks();
  } catch (err) {
    setStatus(err.message, 'err');
  }
});
loadDecks();
