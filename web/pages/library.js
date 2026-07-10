function escapeHtml(value) {
  return String(value ?? '')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#039;');
}

function setStatus(message, kind) {
  const el = document.getElementById('status');
  el.className = kind ? 'status-' + kind : '';
  el.style.display = message ? 'block' : 'none';
  el.textContent = message || '';
}

function formatDuration(seconds) {
  seconds = Number(seconds || 0);
  if (seconds < 60) return '< 1 min';
  const hours = Math.floor(seconds / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  if (!hours) return minutes + ' min';
  return hours + 'h ' + minutes + ' min';
}

function formatNumber(value) {
  return Number(value || 0).toLocaleString();
}

function pct(value) {
  if (value === null || value === undefined) return '-';
  return Math.round(Number(value) * 10) / 10 + '%';
}

function renderSavedBooks(kind, books) {
  if (!books.length) return '<p class="empty">Nothing saved yet</p>';
  return books.map(book => {
    const items = (book.items || []).map(item => {
      const title = item.chapter || ('Spine ' + item.spineIndex);
      const body = kind === 'clipping'
        ? (item.text || '')
        : (item.snippet || ('Progress ' + pct(item.progressPercent)));
      return `<div class="saved-item">
        <div class="saved-item-title">${escapeHtml(title)}</div>
        <div class="saved-item-body">${escapeHtml(body)}</div>
        <div class="item-actions">
          <button class="mini-btn danger" data-action="delete" data-kind="${kind}" data-path="${escapeHtml(book.path)}" data-type="${escapeHtml(book.type)}" data-index="${item.index}">Delete</button>
        </div>
      </div>`;
    }).join('');
    return `<article class="saved-book">
      <div class="saved-header">
        <div>
          <div class="saved-title">${escapeHtml(book.title || book.path)}</div>
          <div class="saved-meta">${escapeHtml(book.author || book.type)} · ${escapeHtml(book.path)} · ${book.count} saved</div>
        </div>
        <div class="item-actions">
          <a class="mini-btn" href="/files?path=${encodeURIComponent(parentPath(book.path))}">Open Folder</a>
          <button class="mini-btn danger" data-action="delete-all" data-kind="${kind}" data-path="${escapeHtml(book.path)}" data-type="${escapeHtml(book.type)}">Clear</button>
        </div>
      </div>
      <div class="saved-items">${items || '<p class="empty">No records loaded</p>'}</div>
    </article>`;
  }).join('');
}

function parentPath(path) {
  const idx = String(path || '').lastIndexOf('/');
  return idx > 0 ? path.slice(0, idx) : '/';
}

async function postJson(url, body) {
  const res = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  const text = await res.text();
  let data = {};
  try { data = text ? JSON.parse(text) : {}; } catch (err) { data = { error: text }; }
  if (!res.ok || data.error) throw new Error(data.error || 'Request failed');
  return data;
}

async function loadSaved() {
  const bookmarksEl = document.getElementById('bookmarksList');
  const clippingsEl = document.getElementById('clippingsList');
  try {
    const res = await fetch('/api/library/saved?_=' + Date.now());
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const data = await res.json();
    const bookmarks = data.bookmarks || [];
    const clippings = data.clippings || [];
    bookmarksEl.innerHTML = renderSavedBooks('bookmark', bookmarks);
    clippingsEl.innerHTML = renderSavedBooks('clipping', clippings);
    document.getElementById('bookmarkSummary').textContent =
      formatNumber(data.bookmarkCount) + ' bookmark' + (data.bookmarkCount === 1 ? '' : 's');
    document.getElementById('clippingSummary').textContent =
      formatNumber(data.clippingCount) + ' clipping' + (data.clippingCount === 1 ? '' : 's');
  } catch (err) {
    bookmarksEl.innerHTML = '<p class="empty">Failed to load bookmarks</p>';
    clippingsEl.innerHTML = '<p class="empty">Failed to load clippings</p>';
  }
}

function renderStatsRows(stats) {
  const rows = [
    ['Reading time', formatDuration(stats.totalReadingSeconds)],
    ['Sessions', formatNumber(stats.totalSessions)],
    ['Pages turned', formatNumber(stats.totalPagesTurned)],
    ['Completed books', formatNumber(stats.completedBooks)],
    ['Current streak', formatNumber(stats.currentStreak) + ' day' + (stats.currentStreak === 1 ? '' : 's')],
    ['Longest streak', formatNumber(stats.longestStreak) + ' day' + (stats.longestStreak === 1 ? '' : 's')]
  ];
  return `<table class="stats-table">${rows.map(([label, value]) => `<tr><th>${label}</th><td>${value}</td></tr>`).join('')}</table>`;
}

function renderBookStats(books) {
  if (!books.length) return '<p class="empty">No recent book stats found</p>';
  return `<table class="stats-table">
    <tr><th>Book</th><th>Time</th><th>Pages</th><th>Status</th></tr>
    ${books.map(book => `<tr>
      <td><strong>${escapeHtml(book.title || book.path)}</strong><br><span class="saved-meta">${escapeHtml(book.path)}</span></td>
      <td>${formatDuration(book.totalReadingSeconds)}<br><span class="saved-meta">${formatNumber(book.sessionCount)} sessions</span></td>
      <td>${formatNumber(book.totalPagesTurned)}</td>
      <td>${book.completed ? 'Complete' : 'Reading'}</td>
    </tr>`).join('')}
  </table>`;
}

async function loadStats() {
  try {
    const res = await fetch('/api/library/stats?_=' + Date.now());
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const data = await res.json();
    const stats = data.aggregated || data.local || {};
    const summary = document.getElementById('statsSummary');
    summary.innerHTML = `
      <div class="metric"><div class="metric-value">${formatDuration(stats.totalReadingSeconds)}</div><div class="metric-label">Reading Time</div></div>
      <div class="metric"><div class="metric-value">${formatNumber(stats.totalSessions)}</div><div class="metric-label">Sessions</div></div>
      <div class="metric"><div class="metric-value">${formatNumber(stats.totalPagesTurned)}</div><div class="metric-label">Pages</div></div>
      <div class="metric"><div class="metric-value">${formatNumber(stats.completedBooks)}</div><div class="metric-label">Completed</div></div>`;
    document.getElementById('statsDetail').innerHTML = renderStatsRows(stats);
    document.getElementById('statsMeta').textContent = data.hasSyncedStats ? 'Aggregated with synced devices' : 'This device only';
    document.getElementById('bookStatsList').innerHTML = renderBookStats(data.recentBooks || []);
  } catch (err) {
    document.getElementById('statsDetail').innerHTML = '<p class="empty">Failed to load reading stats</p>';
    document.getElementById('bookStatsList').innerHTML = '<p class="empty">Failed to load book stats</p>';
  }
}

document.addEventListener('click', async event => {
  const target = event.target.closest('[data-action]');
  if (!target) return;
  const action = target.dataset.action;
  const kind = target.dataset.kind;
  const path = target.dataset.path;
  const type = target.dataset.type;
  const index = target.dataset.index === undefined ? -1 : Number(target.dataset.index);
  const all = action === 'delete-all';
  if (!kind || !path || !type) return;
  if (!confirm(all ? 'Clear all saved items for this book?' : 'Delete this saved item?')) return;
  try {
    await postJson('/api/library/saved/delete', { kind, path, type, index: all ? -1 : index });
    setStatus(all ? 'Saved items cleared' : 'Saved item deleted', 'ok');
    await loadSaved();
  } catch (err) {
    setStatus(err.message, 'err');
  }
});

window.addEventListener('load', () => {
  loadSaved();
  loadStats();
});
