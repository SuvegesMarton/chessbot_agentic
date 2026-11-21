"use strict";

const GLYPHS = {
  K: "\u2654", Q: "\u2655", R: "\u2656", B: "\u2657", N: "\u2658", P: "\u2659",
  k: "\u265A", q: "\u265B", r: "\u265C", b: "\u265D", n: "\u265E", p: "\u265F",
};

const state = {
  fen: "",
  board: new Array(64).fill(null),
  side: "w",
  orientation: "white",
  clocks: { w_ms: null, b_ms: null },
  status: "playing",
  last_move: null,
};

let selected = null;
let legalTargets = new Set();
let moves = [];
let humanColor = "w";
let thinking = false;
let inputLocked = false;
let gameOver = false;

const boardEl = document.getElementById("board");
const connEl = document.getElementById("conn");
const turnEl = document.getElementById("turn");
const gameStatusEl = document.getElementById("game-status");
const fenEl = document.getElementById("fen");
const statusEl = document.getElementById("status-text");
const clockWEl = document.getElementById("clock-w");
const clockBEl = document.getElementById("clock-b");
const overlayEl = document.getElementById("game-over");
const goResultEl = document.getElementById("go-result");
const goReasonEl = document.getElementById("go-reason");

function squareNameToIndex(name) {
  const file = name.charCodeAt(0) - 97;
  const rank = name.charCodeAt(1) - 49;
  if (file < 0 || file > 7 || rank < 0 || rank > 7) return -1;
  return file + rank * 8;
}

function squareName(idx) {
  const file = idx % 8;
  const rank = Math.floor(idx / 8);
  return String.fromCharCode(97 + file) + (rank + 1);
}

function isLight(file, rank) {
  return (file + rank) % 2 === 1;
}

function clearHighlights() {
  selected = null;
  legalTargets.clear();
}

function render() {
  boardEl.textContent = "";
  const white = state.orientation !== "black";
  const last = state.last_move;
  const hiFrom = last ? squareNameToIndex(last.slice(0, 2)) : -1;
  const hiTo = last ? squareNameToIndex(last.slice(2, 4)) : -1;

  for (let row = 0; row < 8; ++row) {
    for (let col = 0; col < 8; ++col) {
      const rank = white ? 7 - row : row;
      const file = white ? col : 7 - col;
      const idx = file + rank * 8;

      const sq = document.createElement("div");
      sq.className = "square " + (isLight(file, rank) ? "light" : "dark");
      if (idx === hiFrom || idx === hiTo) sq.classList.add("highlight");
      if (idx === selected) sq.classList.add("from");
      if (legalTargets.has(idx)) sq.classList.add("legal-target");
      sq.onclick = () => onSquareClick(idx);

      const code = state.board[idx];
      if (code) {
        const span = document.createElement("span");
        span.className = "piece";
        span.textContent = GLYPHS[code] || "";
        span.style.color = code === code.toUpperCase() ? "white" : "#111";
        if (code === code.toUpperCase()) span.style.textShadow = "0 0 3px rgba(0,0,0,0.7)";
        sq.appendChild(span);
      }
      boardEl.appendChild(sq);
    }
  }

  connEl.textContent = connEl.dataset.connected === "1" ? "connected" : "disconnected";
  turnEl.textContent = "side: " + (state.side === "w" ? "white" : "black");
  gameStatusEl.textContent = "status: " + state.status;
  fenEl.textContent = state.fen;

  updateStatusDisplay();
}

function updateStatusDisplay() {
  if (!statusEl) return;
  if (thinking) {
    statusEl.textContent = "thinking...";
    statusEl.classList.add("thinking");
  } else if (inputLocked) {
    statusEl.textContent = "waiting for opponent...";
    statusEl.classList.remove("thinking");
  } else if (state.status === "checkmate") {
    statusEl.textContent = "Checkmate!";
    statusEl.classList.remove("thinking");
  } else if (state.status === "stalemate") {
    statusEl.textContent = "Stalemate";
    statusEl.classList.remove("thinking");
  } else if (state.status === "check") {
    statusEl.textContent = "Check!";
    statusEl.classList.remove("thinking");
  } else {
    statusEl.textContent = "Your move";
    statusEl.classList.remove("thinking");
  }
}

function applyState(msg) {
  if (typeof msg.fen === "string") state.fen = msg.fen;
  if (Array.isArray(msg.board)) state.board = msg.board;
  if (msg.side === "w" || msg.side === "b") state.side = msg.side;
  if (msg.orientation === "white" || msg.orientation === "black") state.orientation = msg.orientation;
  if (msg.clocks) state.clocks = msg.clocks;
  if (typeof msg.status === "string") state.status = msg.status;
  if ("last_move" in msg) state.last_move = msg.last_move;
  clearHighlights();

  humanColor = state.orientation === "white" ? "w" : "b";

  renderClocks();

  if (!gameOver && state.side === humanColor && (state.status === "playing" || state.status === "check")) {
    inputLocked = false;
  } else {
    inputLocked = true;
  }
}

function fmtClock(ms) {
  if (ms === null || ms === undefined) return "--:--";
  if (ms < 0) ms = 0;
  const totalSec = ms / 1000;
  if (ms < 20000) {
    const m = Math.floor(totalSec / 60);
    const s = totalSec - m * 60;
    return m + ":" + (s < 10 ? "0" : "") + s.toFixed(1);
  }
  const m = Math.floor(totalSec / 60);
  const s = Math.floor(totalSec % 60);
  return m + ":" + (s < 10 ? "0" : "") + s;
}

function renderClocks() {
  if (!clockWEl || !clockBEl) return;
  const w = state.clocks ? state.clocks.w_ms : null;
  const b = state.clocks ? state.clocks.b_ms : null;
  clockWEl.textContent = fmtClock(w);
  clockBEl.textContent = fmtClock(b);
  clockWEl.classList.toggle("low", w !== null && w !== undefined && w < 20000);
  clockBEl.classList.toggle("low", b !== null && b !== undefined && b < 20000);
  clockWEl.classList.toggle("active", state.side === "w" && w !== null && w !== undefined);
  clockBEl.classList.toggle("active", state.side === "b" && b !== null && b !== undefined);
}

function handleGameOver(msg) {
  gameOver = true;
  thinking = false;
  inputLocked = true;
  const map = { "1-0": "White wins", "0-1": "Black wins", "1/2": "Draw" };
  if (goResultEl) goResultEl.textContent = map[msg.result] || msg.result;
  if (goReasonEl) goReasonEl.textContent = msg.reason || "";
  if (overlayEl) overlayEl.classList.remove("hidden");
  render();
}

function newGame(kind) {
  gameOver = false;
  if (overlayEl) overlayEl.classList.add("hidden");
  thinking = false;
  inputLocked = true;
  if (kind === "bot") send("newgame bot wtime 300000 btime 300000 winc 2000 binc 2000");
  else send("newgame human");
}

function send(text) {
  if (ws && ws.readyState === WebSocket.OPEN) ws.send(text);
}

function requestLegalMoves(sqIdx) {
  const sqName = squareName(sqIdx);
  send("legal " + sqName);
}

function onSquareClick(idx) {
  if (inputLocked || thinking) return;

  const piece = state.board[idx];
  const isOwnPiece = piece && ((state.side === "w" && piece === piece.toUpperCase()) ||
                                (state.side === "b" && piece === piece.toLowerCase()));

  if (selected === null) {
    if (isOwnPiece) {
      selected = idx;
      requestLegalMoves(idx);
    }
    render();
    return;
  }

  if (idx === selected) {
    clearHighlights();
    render();
    return;
  }

  if (legalTargets.has(idx)) {
    const uci = squareName(selected) + squareName(idx);
    const promoIdx = uci.length === 4 && (idx % 8 === 0 || idx % 8 === 7) &&
                     (state.side === "w" && Math.floor(idx / 8) === 7 ||
                      state.side === "b" && Math.floor(idx / 8) === 0) &&
                     piece && piece.toLowerCase() === 'p';
    let moveUci = uci;
    if (promoIdx) {
      moveUci += "q";
    }
    moves.push(moveUci);
    inputLocked = true;
    thinking = true;
    // Send only the new move incrementally so the server's stateful clock is
    // advanced exactly once (the full-list reconstruction would double-count).
    send("move " + moveUci);
    clearHighlights();
    render();
    return;
  }

  if (isOwnPiece) {
    selected = idx;
    requestLegalMoves(idx);
  } else {
    clearHighlights();
  }
  render();
}

function handleTextMessage(text) {
  if (text.startsWith("info ")) {
    const depthMatch = text.match(/depth\s+(\d+)/);
    const scoreMatch = text.match(/score\s+cp\s+([-\d]+)/);
    thinking = true;
    render();
    return;
  }

  if (text.startsWith("bestmove ")) {
    thinking = false;
    const mv = text.slice(9).trim();
    if (/^[a-h][1-8][a-h][1-8][qrbn]?$/.test(mv)) {
      moves.push(mv);
    }
    render();
    return;
  }
}

let ws = null;
let reconnectTimer = null;

function connect() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  ws = new WebSocket(proto + "://" + location.host + "/ws");

  ws.onopen = () => {
    connEl.dataset.connected = "1";
    if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
    render();
  };

  ws.onmessage = (ev) => {
    const text = ev.data;
    let msg = null;
    try {
      msg = JSON.parse(text);
    } catch (e) {
      msg = null;
    }
    if (msg) {
      if (msg.type === "state" || (typeof msg.fen === "string" && Array.isArray(msg.board))) {
        applyState(msg);
        render();
      } else if (msg.type === "legal") {
        legalTargets.clear();
        if (Array.isArray(msg.moves)) {
          for (const move of msg.moves) {
            const toIdx = squareNameToIndex(move.slice(2, 4));
            if (toIdx >= 0) legalTargets.add(toIdx);
          }
        }
        render();
      } else if (msg.type === "clock") {
        state.clocks = { w_ms: msg.w_ms, b_ms: msg.b_ms };
        renderClocks();
      } else if (msg.type === "game_over") {
        handleGameOver(msg);
      }
    } else {
      handleTextMessage(text);
    }
  };

  ws.onclose = () => {
    connEl.dataset.connected = "0";
    render();
    scheduleReconnect();
  };

  ws.onerror = () => {
    connEl.dataset.connected = "0";
  };
}

function scheduleReconnect() {
  if (reconnectTimer) return;
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connect();
  }, 1500);
}

const btnNewHuman = document.getElementById("btn-new-human");
const btnNewBot = document.getElementById("btn-new-bot");
const btnNewgame = document.getElementById("btn-newgame");
if (btnNewHuman) btnNewHuman.addEventListener("click", () => newGame("human"));
if (btnNewBot) btnNewBot.addEventListener("click", () => newGame("bot"));
if (btnNewgame) btnNewgame.addEventListener("click", () => newGame("human"));

render();
renderClocks();
connect();