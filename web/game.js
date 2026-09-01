"use strict";

const REGION_COLORS = [
  "#f5a9a9", "#f7c99e", "#f2e2a0", "#c5e8a8", "#a8ddd0",
  "#a8d4e8", "#adc0ea", "#c6b3e8", "#dcaee0", "#f2b8d8",
  "#dcc7a8", "#c7ccd4",
];

const EMPTY = 0, MARK = 1, CAT = 2;
const CELL_ICON = { [EMPTY]: "", [MARK]: "✕", [CAT]: "🐱" };
const HEARTS_MAX = 3;
const DOUBLE_TAP_MS = 300;
const DRAG_THRESHOLD_PX = 6;

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
  btnClear: document.getElementById("btn-clear"),
  btnRestart: document.getElementById("btn-restart"),
};

// Per-cell DOM elements, indexed [row][col], created once per level load.
let cellEls = [];

async function init() {
  const res = await fetch("levels_index.json");
  state.sizes = await res.json();
  renderSizeButtons();

  el.btnBack.addEventListener("click", showSelectScreen);
  el.btnClear.addEventListener("click", clearBoard);
  el.btnRestart.addEventListener("click", () => startLevel(state.n, state.levelIdx));

  el.board.addEventListener("pointerdown", onPointerDown);
  el.board.addEventListener("pointermove", onPointerMove);
  el.board.addEventListener("pointerup", onPointerUp);
  el.board.addEventListener("pointercancel", onPointerUp);
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
  el.levelButtons.innerHTML = "";
  for (let i = 1; i <= count; i++) {
    const btn = document.createElement("button");
    btn.textContent = String(i);
    btn.addEventListener("click", () => startLevel(n, i));
    el.levelButtons.appendChild(btn);
  }
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
}

function showSelectScreen() {
  el.screenGame.classList.add("hidden");
  el.screenSelect.classList.remove("hidden");
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
      cellEls[r][c] = cellEl;
      el.board.appendChild(cellEl);
      updateCellView(r, c);
    }
  }
}

function updateCellView(r, c) {
  cellEls[r][c].textContent = CELL_ICON[state.board[r][c]];
}

function renderHearts() {
  el.hearts.textContent = "❤️".repeat(state.hearts) + "🤍".repeat(HEARTS_MAX - state.hearts);
}

function checkWin() {
  const n = state.n;
  let cats = 0;
  for (let r = 0; r < n; r++) for (let c = 0; c < n; c++) if (state.board[r][c] === CAT) cats++;
  if (cats === n) {
    el.statusBanner.textContent = "🎉 完成！唯一解找到了！";
    el.statusBanner.className = "status-banner win";
  }
}

function triggerGameOver() {
  state.gameOver = true;
  el.statusBanner.textContent = "💔 愛心用完了，按「重新開始」再試一次";
  el.statusBanner.className = "status-banner lose";
}

function flashWrong(r, c) {
  const cellEl = cellEls[r][c];
  cellEl.classList.add("wrong");
  setTimeout(() => cellEl.classList.remove("wrong"), 300);
}

function attemptPlaceCat(r, c) {
  if (state.gameOver || state.board[r][c] === CAT) return;
  if (state.solution[r] === c) {
    state.board[r][c] = CAT;
    updateCellView(r, c);
    checkWin();
  } else {
    state.hearts--;
    renderHearts();
    flashWrong(r, c);
    if (state.hearts <= 0) triggerGameOver();
  }
}

function toggleMark(r, c) {
  if (state.gameOver || state.board[r][c] === CAT) return;
  state.board[r][c] = state.board[r][c] === EMPTY ? MARK : EMPTY;
  updateCellView(r, c);
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
let pendingTap = null; // { r, c, timer }

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

  pointerId = e.pointerId;
  el.board.setPointerCapture(pointerId);
  dragOrigin = cell;
  dragging = false;
  lastPaintedKey = null;
  startX = e.clientX;
  startY = e.clientY;

  const startState = state.board[cell.r][cell.c];
  dragTargetState = startState === CAT ? null : (startState === EMPTY ? MARK : EMPTY);
}

function onPointerMove(e) {
  if (e.pointerId !== pointerId) return;
  const cell = cellFromPoint(e.clientX, e.clientY);

  if (!dragging) {
    const moved = Math.hypot(e.clientX - startX, e.clientY - startY) > DRAG_THRESHOLD_PX;
    const leftOrigin = cell && (cell.r !== dragOrigin.r || cell.c !== dragOrigin.c);
    if (!moved && !leftOrigin) return;
    dragging = true;
    paintDragCell(dragOrigin.r, dragOrigin.c);
  }

  if (cell) paintDragCell(cell.r, cell.c);
}

function paintDragCell(r, c) {
  const key = `${r},${c}`;
  if (key === lastPaintedKey) return;
  lastPaintedKey = key;
  if (dragTargetState === null || state.board[r][c] === CAT) return;
  state.board[r][c] = dragTargetState;
  updateCellView(r, c);
}

function onPointerUp(e) {
  if (e.pointerId !== pointerId) return;
  el.board.releasePointerCapture(pointerId);
  const wasDragging = dragging;
  const origin = dragOrigin;
  pointerId = null;
  dragging = false;
  dragOrigin = null;

  if (!wasDragging) handleTap(origin.r, origin.c);
}

function handleTap(r, c) {
  const key = `${r},${c}`;
  if (pendingTap && pendingTap.key === key) {
    clearTimeout(pendingTap.timer);
    pendingTap = null;
    attemptPlaceCat(r, c);
    return;
  }
  pendingTap = {
    key,
    timer: setTimeout(() => {
      pendingTap = null;
      toggleMark(r, c);
    }, DOUBLE_TAP_MS),
  };
}

init();
