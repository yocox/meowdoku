"use strict";

const REGION_COLORS = [
  "#EDB9B9",  // 粉紅
  "#B88357",  // 棕色
  "#EAB226",  // 黃色
  "#CCDCB3",  // 米黃
  "#68B685",  // 暗綠
  "#00FFA2",  // 亮綠
  "#8CADBB",  // 藍灰
  "#5889C3",  // 藍色
  "#3B48BA",  // 深藍
  "#BDA9D0",  // 粉紫
  "#AD59BA",  // 紫色
  "#C71370",  // 桃紅
];

const EMPTY = 0, MARK = 1, CAT = 2, HYPO = 3, WRONG = 4;
const HEARTS_MAX = 3;
const DOUBLE_TAP_MS = 300;
const DRAG_THRESHOLD_PX = 6;

// User-facing toggles — persisted across sessions.
const settings = (() => {
  try {
    const s = JSON.parse(localStorage.getItem("meowdoku_settings") || "{}");
    return { sound: s.sound !== false, vibrate: s.vibrate !== false, autoElim: s.autoElim !== false, hypo: !!s.hypo };
  } catch { return { sound: true, vibrate: true, autoElim: true, hypo: false }; }
})();

function saveSettings() {
  try { localStorage.setItem("meowdoku_settings", JSON.stringify(settings)); } catch { }
}

// Star tracking: { "n:idx": 1|2|3 }. Migrates old array format to object.
function getStars() {
  try {
    const raw = JSON.parse(localStorage.getItem("meowdoku_done") || "{}");
    if (Array.isArray(raw)) { const o = {}; raw.forEach(k => { o[k] = 1; }); return o; }
    return (typeof raw === "object" && raw !== null) ? raw : {};
  } catch { return {}; }
}
function saveStars(n, idx, stars) {
  const data = getStars();
  const key = `${n}:${idx}`;
  if ((data[key] || 0) < stars) data[key] = stars;
  try { localStorage.setItem("meowdoku_done", JSON.stringify(data)); } catch { }
}

const state = {
  sizes: {},        // { "8": levelCount, ... }
  n: null,
  levelIdx: null,
  regions: null,     // n x n array of region ids (0..n-1)
  solution: null,    // solution[row] = column of the true cat
  board: null,       // n x n array of EMPTY/MARK/CAT
  hearts: HEARTS_MAX,
  gameOver: false,
};

const el = {
  sizeButtons: document.getElementById("size-buttons"),
  levelButtons: document.getElementById("level-buttons"),
  screenSelect: document.getElementById("screen-select"),
  screenGame: document.getElementById("screen-game"),
  board: document.getElementById("board"),
  gameTitle: document.getElementById("game-title"),
  hearts: document.getElementById("hearts"),
  statusBanner: document.getElementById("status-banner"),
  btnBack: document.getElementById("btn-back"),
  btnRestart: document.getElementById("btn-restart"),
  winModal: document.getElementById("win-modal"),
  btnNextLevel: document.getElementById("btn-next-level"),
  btnReplay: document.getElementById("btn-replay"),
  btnModalBack: document.getElementById("btn-modal-back"),
  helpModal: document.getElementById("help-modal"),
  btnHelp: document.getElementById("btn-help"),
  btnHelpClose: document.getElementById("btn-help-close"),
  btnToggleSound: document.getElementById("btn-toggle-sound"),
  btnToggleVibrate: document.getElementById("btn-toggle-vibrate"),
  btnToggleAuto: document.getElementById("btn-toggle-auto"),
  btnToggleHypo: document.getElementById("btn-toggle-hypo"),
};

// ── Audio ────────────────────────────────────────────────────────────────────
// All sounds are synthesised with Web Audio API — no external assets needed.

let _ctx = null;
function _getCtx() {
  if (!_ctx) _ctx = new (window.AudioContext || window.webkitAudioContext)();
  if (_ctx.state === "suspended") _ctx.resume();
  return _ctx;
}

function _tone(freq, type, vol, dur, t) {
  const ctx = _getCtx();
  const s = t !== undefined ? t : ctx.currentTime;
  const osc = ctx.createOscillator();
  const gain = ctx.createGain();
  osc.type = type;
  osc.frequency.setValueAtTime(freq, s);
  gain.gain.setValueAtTime(vol, s);
  gain.gain.exponentialRampToValueAtTime(0.001, s + dur);
  osc.connect(gain);
  gain.connect(ctx.destination);
  osc.start(s);
  osc.stop(s + dur);
}

// Short soft tick for marking/unmarking cells (including each cell during drag).
function playMark() { if (settings.sound) _tone(660, "sine", 0.08, 0.10); }

// Warm ding (fundamental + octave) when a cat is placed correctly.
function playCat() {
  if (!settings.sound) return;
  _tone(880, "sine", 0.18, 0.45); _tone(1760, "sine", 0.08, 0.45);
}

// Sharp descending buzz when a cat guess is wrong.
function playWrong() {
  if (!settings.sound) return;
  const ctx = _getCtx(), t = ctx.currentTime;
  const osc = ctx.createOscillator(), gain = ctx.createGain();
  osc.type = "square";
  osc.frequency.setValueAtTime(240, t);
  osc.frequency.linearRampToValueAtTime(110, t + 0.40);
  gain.gain.setValueAtTime(0.12, t);
  gain.gain.exponentialRampToValueAtTime(0.001, t + 0.40);
  osc.connect(gain); gain.connect(ctx.destination);
  osc.start(t); osc.stop(t + 0.40);
}

// C-major arpeggio (C E G C) spread over 0.3 s for the win moment.
function playWin() {
  if (!settings.sound) return;
  const ctx = _getCtx(), now = ctx.currentTime;
  [523, 659, 784, 1047].forEach((f, i) => _tone(f, "sine", 0.18, 0.4, now + i * 0.1));
}

// ── Haptic ───────────────────────────────────────────────────────────────────

function vibrate(ms) { if (settings.vibrate && navigator.vibrate) navigator.vibrate(ms); }

// Per-cell DOM elements, indexed [row][col], created once per level load.
let cellEls = [];

function updateToggleUI() {
  el.btnToggleSound.textContent = settings.sound ? "🔊" : "🔇";
  el.btnToggleVibrate.textContent = settings.vibrate ? "📳" : "📴";
  el.btnToggleSound.classList.toggle("off", !settings.sound);
  el.btnToggleVibrate.classList.toggle("off", !settings.vibrate);
  el.btnToggleAuto.classList.toggle("off", !settings.autoElim);
  el.btnToggleHypo?.classList.toggle("off", !settings.hypo);
}

async function init() {
  // Bind all event listeners synchronously BEFORE any async operations so that
  // browser caching of an older JS file can never leave buttons unresponsive.
  updateToggleUI();
  try { history.replaceState({ screen: "select" }, ""); } catch { }

  el.btnBack.addEventListener("click", () => history.back());
  el.btnRestart.addEventListener("click", () => startLevel(state.n, state.levelIdx));
  el.btnNextLevel?.addEventListener("click", () => {
    el.winModal.classList.add("hidden");
    startLevel(state.n, state.levelIdx + 1);
  });
  el.btnReplay?.addEventListener("click", () => {
    el.winModal.classList.add("hidden");
    startLevel(state.n, state.levelIdx);
  });
  el.btnModalBack?.addEventListener("click", () => {
    el.winModal.classList.add("hidden");
    history.back();
  });

  el.btnHelp?.addEventListener("click", () => el.helpModal.classList.remove("hidden"));
  el.btnHelpClose?.addEventListener("click", () => el.helpModal.classList.add("hidden"));
  el.helpModal?.addEventListener("click", (e) => {
    if (e.target === el.helpModal) el.helpModal.classList.add("hidden");
  });

  window.addEventListener("popstate", () => {
    if (!el.screenGame.classList.contains("hidden")) {
      el.winModal.classList.add("hidden");
      showSelectScreen();
    }
  });

  el.btnToggleSound?.addEventListener("click", () => {
    settings.sound = !settings.sound;
    saveSettings();
    updateToggleUI();
  });
  el.btnToggleVibrate?.addEventListener("click", () => {
    settings.vibrate = !settings.vibrate;
    saveSettings();
    updateToggleUI();
  });
  el.btnToggleAuto?.addEventListener("click", () => {
    settings.autoElim = !settings.autoElim;
    saveSettings();
    updateToggleUI();
  });
  el.btnToggleHypo?.addEventListener("click", () => {
    settings.hypo = !settings.hypo;
    saveSettings();
    updateToggleUI();
  });

  el.board.addEventListener("pointerdown", onPointerDown);
  el.board.addEventListener("pointermove", onPointerMove);
  el.board.addEventListener("pointerup", onPointerUp);
  el.board.addEventListener("pointercancel", onPointerUp);

  const res = await fetch("levels_index.json");
  state.sizes = await res.json();
  renderSizeButtons();
}

function renderSizeButtons() {
  el.sizeButtons.innerHTML = "";
  Object.keys(state.sizes).sort((a, b) => a - b).forEach((n) => {
    const btn = document.createElement("button");
    btn.textContent = `${n} x ${n}`;
    btn.addEventListener("click", () => selectSize(Number(n)));
    el.sizeButtons.appendChild(btn);
  });
}

function selectSize(n) {
  state.n = n;
  [...el.sizeButtons.children].forEach((b) => {
    b.classList.toggle("selected", b.textContent.startsWith(`${n} `));
  });

  const count = state.sizes[n];
  const stars = getStars();
  el.levelButtons.innerHTML = "";
  for (let i = 1; i <= count; i++) {
    const btn = document.createElement("button");
    btn.textContent = String(i);
    const s = stars[`${n}:${i}`] || 0;
    if (s > 0) { btn.classList.add("done"); btn.dataset.stars = String(s); }
    btn.addEventListener("click", () => startLevel(n, i));
    el.levelButtons.appendChild(btn);
  }
}

function refreshDoneMarks() {
  if (!state.n) return;
  const stars = getStars();
  [...el.levelButtons.children].forEach((btn, i) => {
    const s = stars[`${state.n}:${i + 1}`] || 0;
    btn.classList.toggle("done", s > 0);
    if (s > 0) btn.dataset.stars = String(s);
    else delete btn.dataset.stars;
  });
}

async function startLevel(n, idx) {
  const path = `levels/${n}/level_${n}_${String(idx).padStart(3, "0")}.txt`;
  const res = await fetch(path);
  const text = await res.text();
  const { regions, solution } = parseLevel(text);

  state.n = n;
  state.levelIdx = idx;
  state.regions = regions;
  state.solution = solution;
  state.board = Array.from({ length: n }, () => Array(n).fill(EMPTY));
  state.hearts = HEARTS_MAX;
  state.gameOver = false;

  el.gameTitle.textContent = `${n} x ${n} — 第 ${idx} 關`;
  el.statusBanner.classList.add("hidden");
  showGameScreen();
  renderBoard();
  renderHearts();
}

function parseLevel(text) {
  const allLines = text.split("\n");
  const solutionLine = allLines.find((l) => l.startsWith("# solution:"));
  const solution = solutionLine.replace("# solution:", "").trim().split(/\s+/).map(Number);

  const lines = allLines.filter((l) => !l.startsWith("#") && l.trim() !== "");
  const n = parseInt(lines[0], 10);
  const regions = [];
  for (let r = 0; r < n; r++) {
    regions.push(lines[1 + r].split("").map((ch) => ch.charCodeAt(0) - 65));
  }
  return { n, regions, solution };
}

function showGameScreen() {
  el.screenSelect.classList.add("hidden");
  el.screenGame.classList.remove("hidden");
  // Push only when coming from the select screen; replace when already in-game (next level).
  if (history.state?.screen !== "game") history.pushState({ screen: "game" }, "");
  else history.replaceState({ screen: "game" }, "");
}

function showSelectScreen() {
  el.screenGame.classList.add("hidden");
  el.screenSelect.classList.remove("hidden");
  refreshDoneMarks();
}

function clearBoard() {
  const n = state.n;
  state.board = Array.from({ length: n }, () => Array(n).fill(EMPTY));
  for (let r = 0; r < n; r++) for (let c = 0; c < n; c++) updateCellView(r, c);
}

function renderBoard() {
  const n = state.n;
  el.board.style.gridTemplateColumns = `repeat(${n}, 1fr)`;
  el.board.style.gridTemplateRows = `repeat(${n}, 1fr)`;
  el.board.innerHTML = "";

  cellEls = Array.from({ length: n }, () => Array(n));
  for (let r = 0; r < n; r++) {
    for (let c = 0; c < n; c++) {
      const cellEl = document.createElement("div");
      cellEl.className = "cell";
      cellEl.style.background = REGION_COLORS[state.regions[r][c] % REGION_COLORS.length];
      cellEl.innerHTML = '<span class="mark"><span class="bar"></span><span class="bar"></span></span>'
        + '<span class="cat-icon">🐱</span>'
        + '<span class="hypo-icon">△</span>';
      cellEls[r][c] = cellEl;
      el.board.appendChild(cellEl);
      updateCellView(r, c);
    }
  }
}

function updateCellView(r, c) {
  cellEls[r][c].dataset.state = String(state.board[r][c]);
}

function renderHearts() {
  el.hearts.textContent = "❤️".repeat(state.hearts) + "🤍".repeat(HEARTS_MAX - state.hearts);
}

function checkWin() {
  const n = state.n;
  let cats = 0;
  for (let r = 0; r < n; r++) for (let c = 0; c < n; c++) if (state.board[r][c] === CAT) cats++;
  if (cats === n) {
    state.gameOver = true;
    saveStars(state.n, state.levelIdx, state.hearts);
    playWin(); vibrate(300);
    const hasNext = state.levelIdx < state.sizes[state.n];
    el.btnNextLevel.style.display = hasNext ? "" : "none";
    el.winModal.classList.remove("hidden");
  }
}

function triggerGameOver() {
  state.gameOver = true;
  el.statusBanner.textContent = "💔 掰了，按「重來」再試一次";
  el.statusBanner.className = "status-banner lose";
}


// Auto-eliminate: when a cat is correctly placed, mark same row, same column,
// surrounding 8 cells, and entire same-region (same color) as MARK.
function autoEliminate(r, c) {
  const n = state.n;
  const region = state.regions[r][c];
  const mark = (mr, mc) => {
    if (state.board[mr][mc] === EMPTY) { state.board[mr][mc] = MARK; updateCellView(mr, mc); }
  };
  for (let j = 0; j < n; j++) if (j !== c) mark(r, j);
  for (let i = 0; i < n; i++) if (i !== r) mark(i, c);
  for (let dr = -1; dr <= 1; dr++)
    for (let dc = -1; dc <= 1; dc++) {
      if (dr === 0 && dc === 0) continue;
      const nr = r + dr, nc = c + dc;
      if (nr >= 0 && nr < n && nc >= 0 && nc < n) mark(nr, nc);
    }
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++)
      if (state.regions[i][j] === region) mark(i, j);
}

function attemptPlaceCat(r, c) {
  if (state.gameOver || state.board[r][c] === CAT || state.board[r][c] === WRONG) return;
  if (state.solution[r] === c) {
    state.board[r][c] = CAT;
    updateCellView(r, c);
    playCat(); vibrate(100);
    if (settings.autoElim) autoEliminate(r, c);
    checkWin();
  } else {
    state.hearts--;
    renderHearts();
    state.board[r][c] = WRONG;
    updateCellView(r, c);
    playWrong(); vibrate(200);
    if (state.hearts <= 0) triggerGameOver();
  }
}

function toggleMark(r, c) {
  if (state.gameOver || state.board[r][c] === CAT || state.board[r][c] === WRONG) return;
  state.board[r][c] = state.board[r][c] === EMPTY ? MARK : EMPTY;
  updateCellView(r, c);
  playMark(); vibrate(50);
}

function toggleHypo(r, c) {
  if (state.gameOver || state.board[r][c] === CAT || state.board[r][c] === WRONG) return;
  state.board[r][c] = state.board[r][c] === HYPO ? EMPTY : HYPO;
  updateCellView(r, c);
  playMark(); vibrate(50);
}

// --- Pointer handling: single tap toggles a mark, double tap on the same
// cell attempts a cat, and press-and-drag paints every swept cell to match
// the cell where the drag started (not a per-cell toggle). ---

let pointerId = null;
let dragging = false;
let dragTargetState = null;
let dragOrigin = null;
let lastPaintedKey = null;
let startX = 0, startY = 0;
let pendingTap = null; // { key, prevState, timer }
let activeCellEl = null;

function cellFromPoint(clientX, clientY) {
  const n = state.n;
  const rect = el.board.getBoundingClientRect();
  const relX = clientX - rect.left, relY = clientY - rect.top;
  if (relX < 0 || relY < 0 || relX >= rect.width || relY >= rect.height) return null;
  const c = Math.floor((relX / rect.width) * n);
  const r = Math.floor((relY / rect.height) * n);
  if (r < 0 || r >= n || c < 0 || c >= n) return null;
  return { r, c };
}

function onPointerDown(e) {
  if (state.gameOver || pointerId !== null) return;
  const cell = cellFromPoint(e.clientX, e.clientY);
  if (!cell) return;
  e.preventDefault(); // only suppress default when pointer is actually over the board

  const startState = state.board[cell.r][cell.c];
  if (startState !== WRONG) {
    activeCellEl = cellEls[cell.r][cell.c];
    activeCellEl.classList.add("active");
  }

  pointerId = e.pointerId;
  el.board.setPointerCapture(pointerId);
  dragOrigin = cell;
  dragging = false;
  lastPaintedKey = null;
  startX = e.clientX;
  startY = e.clientY;

  if (startState === CAT || startState === WRONG) dragTargetState = null;
  else if (settings.hypo) dragTargetState = startState === HYPO ? EMPTY : HYPO;
  else dragTargetState = startState === EMPTY ? MARK : EMPTY;
}

function onPointerMove(e) {
  if (e.pointerId !== pointerId) return;
  const cell = cellFromPoint(e.clientX, e.clientY);

  if (!dragging) {
    const moved = Math.hypot(e.clientX - startX, e.clientY - startY) > DRAG_THRESHOLD_PX;
    const leftOrigin = cell && (cell.r !== dragOrigin.r || cell.c !== dragOrigin.c);
    if (!moved && !leftOrigin) return;
    dragging = true;
    if (activeCellEl) { activeCellEl.classList.remove("active"); activeCellEl = null; }
    paintDragCell(dragOrigin.r, dragOrigin.c);
  }

  if (cell) paintDragCell(cell.r, cell.c);
}

function paintDragCell(r, c) {
  const key = `${r},${c}`;
  if (key === lastPaintedKey) return;
  lastPaintedKey = key;
  if (dragTargetState === null || state.board[r][c] === CAT || state.board[r][c] === WRONG) return;
  if (state.board[r][c] === dragTargetState) return;
  const cur = state.board[r][c];
  if (settings.hypo ? (cur !== EMPTY && cur !== HYPO) : (cur !== EMPTY && cur !== MARK)) return;
  state.board[r][c] = dragTargetState;
  updateCellView(r, c);
  playMark(); vibrate(100);
}

function onPointerUp(e) {
  if (e.pointerId !== pointerId) return;
  el.board.releasePointerCapture(pointerId);
  if (activeCellEl) { activeCellEl.classList.remove("active"); activeCellEl = null; }
  const wasDragging = dragging;
  const origin = dragOrigin;
  pointerId = null;
  dragging = false;
  dragOrigin = null;

  if (!wasDragging) handleTap(origin.r, origin.c);
}

function handleTap(r, c) {
  if (state.board[r][c] === WRONG) return;
  const key = `${r},${c}`;
  if (pendingTap && pendingTap.key === key) {
    clearTimeout(pendingTap.timer);
    // Undo the mark applied on first tap, then place cat
    if (pendingTap.prevState !== undefined) {
      state.board[r][c] = pendingTap.prevState;
      updateCellView(r, c);
    }
    pendingTap = null;
    attemptPlaceCat(r, c);
    return;
  }
  // Flush any pending tap on a different cell
  if (pendingTap) {
    clearTimeout(pendingTap.timer);
    pendingTap = null;
  }
  // Apply mark/hypo immediately for instant feedback
  const prevState = state.board[r][c];
  if (settings.hypo) toggleHypo(r, c); else toggleMark(r, c);
  pendingTap = {
    key,
    prevState,
    timer: setTimeout(() => { pendingTap = null; }, DOUBLE_TAP_MS),
  };
}

init();
