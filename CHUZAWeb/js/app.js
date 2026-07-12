// View/state controller for CHUZA Web - a JS port of CHUZAControls/app.py
// and its screens/*.py, wired to net.js's cloud-only MqttBridge. Screens
// are plain DOM sections toggled via a `.active` class instead of Tk
// window geometry swaps.
import {
  DEFAULT_CONFIG,
  SETTINGS_SCHEMA,
  SETTINGS_CHOICES,
  BASE_SPEED,
  BOOST_SPEED,
  TURN_BIAS,
  CONTROL_TICK_MS,
  DISTANCE_OUT_OF_RANGE_MM,
} from "./schema.js";
import { tryLoadConfig, saveConfig } from "./config.js";
import { MqttBridge } from "./net.js";

// ---------- state ----------
let config = null;
let bridge = null;
let telemetry = {};
let settingsCache = {};
let settingsInputs = {};
let settingsTabButtons = [];
const pressed = new Set();
let controlActive = false;
let controlTimer = null;
let camWantedOn = false;
let lastFrameBlob = null;
let lastFrameUrl = null;
let frameTimes = [];
let wakeLock = null;

// ---------- screens ----------
function showScreen(name) {
  for (const el of document.querySelectorAll(".screen")) {
    el.classList.toggle("active", el.id === `screen-${name}`);
  }
}

// ---------- toast ----------
function showToast(message, kind = "info", durationMs = 2200) {
  const el = document.getElementById("toast");
  clearTimeout(showToast._timer);
  el.textContent = message;
  el.classList.remove("hidden", "warning", "error");
  if (kind !== "info") el.classList.add(kind);
  showToast._timer = setTimeout(() => el.classList.add("hidden"), durationMs);
}

// ---------- connection form (shared by Login and Settings > Change Connection) ----------
function buildConnectionForm(mountEl, initialValues, onSave, saveLabel) {
  mountEl.innerHTML = "";
  const tmpl = document.getElementById("tmpl-connection-form");
  const frag = tmpl.content.cloneNode(true);
  const form = frag.querySelector("form");

  for (const el of form.elements) {
    if (el.name && initialValues[el.name] !== undefined) {
      el.value = initialValues[el.name];
    }
  }
  form.querySelector(".save-btn").textContent = saveLabel;
  const errorEl = form.querySelector(".form-error");

  form.addEventListener("submit", (e) => {
    e.preventDefault();
    const fields = {};
    for (const el of form.elements) {
      if (el.name) fields[el.name] = el.value.trim();
    }
    if (!fields.broker_host || !fields.username || !fields.password) {
      errorEl.textContent = "Host, username, and password are required.";
      return;
    }
    const port = parseInt(fields.broker_port, 10);
    if (!Number.isFinite(port)) {
      errorEl.textContent = "Port must be a number.";
      return;
    }
    fields.broker_port = port;
    errorEl.textContent = "";
    onSave(fields);
  });

  mountEl.appendChild(frag);
}

function showLoginForNewConnection() {
  buildConnectionForm(
    document.getElementById("login-form-mount"),
    config || DEFAULT_CONFIG,
    (fields) => {
      const saved = saveConfig(fields);
      connectWithConfig(saved);
      showScreen("home");
    },
    "CONNECT"
  );
  showScreen("login");
}

function openChangeConnection() {
  buildConnectionForm(
    document.getElementById("connection-form-mount"),
    config || DEFAULT_CONFIG,
    (fields) => {
      const saved = saveConfig(fields);
      closeChangeConnection();
      connectWithConfig(saved);
      showScreen("home");
      showToast("Reconnecting...");
    },
    "SAVE & RECONNECT"
  );
  document.getElementById("connection-overlay").classList.remove("hidden");
}

function closeChangeConnection() {
  document.getElementById("connection-overlay").classList.add("hidden");
}

// ---------- networking ----------
function connectWithConfig(cfg) {
  bridge?.close();
  telemetry = {};
  settingsCache = {};
  config = cfg;
  bridge = new MqttBridge(cfg, {
    onStatus: applyStatus,
    onTelemetry: applyTelemetry,
    onFrame: applyFrame,
    onSettings: applySettings,
  });
}

function sendCommand(cmd) {
  bridge?.publishCommand(cmd);
}

// ---------- status ----------
function applyStatus(raw) {
  const status = raw === "online" || raw === "offline" ? raw : "connecting";
  const color = { online: "var(--accent)", offline: "var(--critical)", connecting: "var(--warning)" }[status];

  const homeStatus = document.getElementById("home-status");
  homeStatus.textContent = `● ${{ online: "ONLINE", offline: "OFFLINE", connecting: "CONNECTING..." }[status]}`;
  homeStatus.style.color = color;
  document.getElementById("play-btn").disabled = status !== "online";

  const controlStatus = document.getElementById("control-status");
  controlStatus.textContent = `● ${{ online: "ONLINE", offline: "OFFLINE", connecting: "CONNECTING" }[status]}`;
  controlStatus.style.color = color;

  if (status === "offline" && controlActive) {
    stopControlLoop();
    releaseWakeLock();
    showScreen("home");
    showToast("Lost connection to the robot.", "error");
  }
}

// ---------- telemetry ----------
function applyTelemetry(data) {
  Object.assign(telemetry, data);
  const t = telemetry;

  if (["tempC", "humidity", "pressureHpa", "altitudeM"].some((k) => k in data)) {
    let line1 = t.tempC != null ? `${t.tempC.toFixed(1)}°C` : "--";
    if (t.humidity != null) line1 += `   ${t.humidity.toFixed(0)}% RH`;
    const line2Parts = [];
    if (t.pressureHpa != null) line2Parts.push(`${t.pressureHpa.toFixed(0)} hPa`);
    if (t.altitudeM != null) line2Parts.push(`${t.altitudeM.toFixed(0)} m`);
    document.getElementById("env-line1").textContent = line1;
    document.getElementById("env-line2").textContent = line2Parts.length ? line2Parts.join("   ") : "--";
  }

  if ("battPct" in data || "battV" in data) {
    const pct = t.battPct ?? 0;
    const fill = document.getElementById("batt-fill");
    fill.style.width = `${Math.max(0, Math.min(100, pct))}%`;
    fill.classList.toggle("low", pct <= 20);
    const suffix = t.battV != null ? `  (${t.battV.toFixed(2)}V)` : "";
    document.getElementById("batt-text").textContent = `${pct}%${suffix}`;
  }

  if ("distanceMm" in data || "chipTempC" in data) {
    const mm = t.distanceMm;
    let line1 = "--";
    if (mm != null) line1 = mm >= DISTANCE_OUT_OF_RANGE_MM ? "Out of range" : `${mm} mm`;
    document.getElementById("dist-line1").textContent = line1;
    document.getElementById("dist-line2").textContent =
      t.chipTempC != null ? `Chip: ${t.chipTempC.toFixed(1)}°C` : "Chip: --";
  }

  if ("camOverheat" in data) {
    document.getElementById("overheat-badge").classList.toggle("hidden", !data.camOverheat);
  }

  if ("camOn" in data) {
    setCamLabel(data.camOn);
    camWantedOn = data.camOn;
    if (!data.camOn) resetVideo();
  }
}

// ---------- settings ----------
function applySettings(data) {
  Object.assign(settingsCache, data);
  renderSettingsValues();
}

function requestSettingsRefresh() {
  sendCommand({ cmd: "get_settings" });
}

function buildSettingsScreen() {
  const tabsEl = document.getElementById("settings-tabs");
  const contentEl = document.getElementById("settings-content");
  tabsEl.innerHTML = "";
  contentEl.innerHTML = "";
  settingsInputs = {};

  for (const [section, fields] of Object.entries(SETTINGS_SCHEMA)) {
    const tabBtn = document.createElement("button");
    tabBtn.type = "button";
    tabBtn.className = "btn";
    tabBtn.textContent = section.toUpperCase();
    tabBtn.addEventListener("click", () => showSettingsSection(section));
    tabsEl.appendChild(tabBtn);

    const sectionEl = document.createElement("div");
    sectionEl.className = "settings-section";
    sectionEl.dataset.section = section;
    sectionEl.style.display = "none";

    for (const [key, label, kind, low, high] of fields) {
      const row = document.createElement("div");
      row.className = "settings-row";
      const labelEl = document.createElement("label");
      labelEl.textContent = label;
      row.appendChild(labelEl);

      let input;
      if (kind === "bool") {
        input = document.createElement("input");
        input.type = "checkbox";
      } else if (kind === "choice") {
        input = document.createElement("select");
        for (const choice of SETTINGS_CHOICES[key]) {
          const opt = document.createElement("option");
          opt.value = choice;
          opt.textContent = choice;
          input.appendChild(opt);
        }
      } else {
        input = document.createElement("input");
        input.type = "number";
        input.min = String(low);
        input.max = String(high);
        if (kind === "float") input.step = "0.01";
      }
      row.appendChild(input);
      sectionEl.appendChild(row);
      settingsInputs[key] = { kind, el: input };
    }

    contentEl.appendChild(sectionEl);
  }

  settingsTabButtons = Array.from(tabsEl.children);
  showSettingsSection(Object.keys(SETTINGS_SCHEMA)[0]);
}

function showSettingsSection(section) {
  for (const el of document.querySelectorAll(".settings-section")) {
    el.style.display = el.dataset.section === section ? "block" : "none";
  }
  for (const btn of settingsTabButtons) {
    btn.classList.toggle("active", btn.textContent === section.toUpperCase());
  }
}

function renderSettingsValues() {
  for (const [key, { kind, el }] of Object.entries(settingsInputs)) {
    if (!(key in settingsCache)) continue;
    const value = settingsCache[key];
    if (kind === "bool") {
      el.checked = Boolean(value);
    } else if (kind === "choice") {
      const choices = SETTINGS_CHOICES[key];
      el.value = typeof value === "number" ? choices[value] : value;
    } else {
      el.value = value;
    }
  }
  document.getElementById("settings-status").textContent = "";
}

function collectSettings() {
  const out = {};
  for (const [key, { kind, el }] of Object.entries(settingsInputs)) {
    if (kind === "bool") out[key] = el.checked;
    else if (kind === "choice") out[key] = SETTINGS_CHOICES[key].indexOf(el.value);
    else if (kind === "float") out[key] = parseFloat(el.value);
    else out[key] = parseInt(el.value, 10);
  }
  return out;
}

// ---------- camera ----------
function setCamLabel(on) {
  document.getElementById("btn-cam").textContent = `CAM: ${on ? "ON" : "OFF"}`;
}

function toggleCamera() {
  camWantedOn = !camWantedOn;
  setCamLabel(camWantedOn);
  sendCommand({ cmd: camWantedOn ? "cam_on" : "cam_off" });
  if (!camWantedOn) resetVideo();
}

function resetVideo() {
  frameTimes = [];
  document.getElementById("link-fps").textContent = "0 fps";
  if (lastFrameUrl) {
    URL.revokeObjectURL(lastFrameUrl);
    lastFrameUrl = null;
  }
  const img = document.getElementById("video-img");
  img.classList.remove("has-frame");
  img.removeAttribute("src");
}

function applyFrame(payload) {
  if (!controlActive) return; // a frame arrived after we already navigated away
  const blob = new Blob([payload], { type: "image/jpeg" });
  lastFrameBlob = blob;
  const url = URL.createObjectURL(blob);
  const img = document.getElementById("video-img");
  img.src = url;
  img.classList.add("has-frame");
  if (lastFrameUrl) URL.revokeObjectURL(lastFrameUrl);
  lastFrameUrl = url;

  const now = performance.now();
  frameTimes.push(now);
  while (frameTimes.length && now - frameTimes[0] > 1000) frameTimes.shift();
  document.getElementById("link-fps").textContent = `${frameTimes.length} fps`;
}

function capturePhoto() {
  if (!lastFrameBlob) {
    showToast("No camera frame yet - turn the camera on first.", "warning");
    return;
  }
  const url = URL.createObjectURL(lastFrameBlob);
  const stamp = new Date().toISOString().replace(/[-:T]/g, "").slice(0, 15);
  const a = document.createElement("a");
  a.href = url;
  a.download = `chuza_${stamp}.jpg`;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 30000);
  showToast(`Saved: ${a.download}`);
}

// ---------- timer dialog ----------
function openTimerDialog() {
  document.getElementById("timer-overlay").classList.remove("hidden");
}
function closeTimerDialog() {
  document.getElementById("timer-overlay").classList.add("hidden");
}

// ---------- driving ----------
function computeTargets() {
  const boost = pressed.has("boost");
  const base = boost ? BOOST_SPEED : BASE_SPEED;
  const w = pressed.has("w");
  const s = pressed.has("s");
  const a = pressed.has("a");
  const d = pressed.has("d");

  let left = 0;
  let right = 0;

  if (w) {
    left += base;
    right += base;
  }
  if (s) {
    left -= base;
    right -= base;
  }

  if (w) {
    if (d) {
      left += TURN_BIAS;
      right -= TURN_BIAS;
    }
    if (a) {
      left -= TURN_BIAS;
      right += TURN_BIAS;
    }
  } else if (s) {
    if (d) {
      left -= TURN_BIAS;
      right += TURN_BIAS;
    }
    if (a) {
      left += TURN_BIAS;
      right -= TURN_BIAS;
    }
  } else {
    if (d) {
      left += base;
      right -= base;
    }
    if (a) {
      left -= base;
      right += base;
    }
  }

  left = Math.max(-100, Math.min(100, left));
  right = Math.max(-100, Math.min(100, right));
  return [left, right];
}

function controlTick() {
  if (!controlActive) return;
  if (pressed.has("brake")) {
    sendCommand({ cmd: "brake" });
    document.getElementById("target-readout").textContent = "BRAKE   BRAKE";
  } else {
    const [left, right] = computeTargets();
    sendCommand({ cmd: "move", left, right });
    document.getElementById("target-readout").textContent = `L: ${left}%   R: ${right}%`;
  }
}

function startControlLoop() {
  controlActive = true;
  controlTimer = setInterval(controlTick, CONTROL_TICK_MS);
}

function stopControlLoop() {
  controlActive = false;
  clearInterval(controlTimer);
  controlTimer = null;
  pressed.clear();
  for (const el of document.querySelectorAll(".pressed")) el.classList.remove("pressed");
}

function setupPressButtons() {
  for (const el of document.querySelectorAll("[data-key]")) {
    const key = el.dataset.key;
    el.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      el.setPointerCapture(e.pointerId);
      pressed.add(key);
      el.classList.add("pressed");
    });
    const release = () => {
      pressed.delete(key);
      el.classList.remove("pressed");
    };
    el.addEventListener("pointerup", release);
    el.addEventListener("pointercancel", release);
  }
}

// ---------- wake lock (nice-to-have, feature-detected) ----------
async function requestWakeLock() {
  try {
    if ("wakeLock" in navigator) wakeLock = await navigator.wakeLock.request("screen");
  } catch {
    // not fatal - screen may just dim during a drive on unsupported browsers
  }
}

function releaseWakeLock() {
  wakeLock?.release().catch(() => {});
  wakeLock = null;
}

// ---------- navigation ----------
function goHome() {
  stopControlLoop();
  sendCommand({ cmd: "stop" });
  if (camWantedOn) {
    camWantedOn = false;
    setCamLabel(false);
    sendCommand({ cmd: "cam_off" });
  }
  resetVideo();
  releaseWakeLock();
  showScreen("home");
}

function enterControl() {
  showScreen("control");
  startControlLoop();
  requestWakeLock();
}

function enterSettings() {
  showScreen("settings");
  requestSettingsRefresh();
}

// ---------- wiring ----------
document.getElementById("play-btn").addEventListener("click", enterControl);
document.getElementById("home-change-connection-btn").addEventListener("click", showLoginForNewConnection);
document.getElementById("control-back-btn").addEventListener("click", goHome);
document.getElementById("btn-settings").addEventListener("click", enterSettings);
document.getElementById("btn-timer").addEventListener("click", openTimerDialog);
document.getElementById("btn-capture").addEventListener("click", capturePhoto);
document.getElementById("btn-cam").addEventListener("click", toggleCamera);

document.getElementById("timer-set-btn").addEventListener("click", () => {
  const input = document.getElementById("timer-minutes");
  const minutes = Math.max(1, Math.min(60, parseInt(input.value, 10) || 5));
  sendCommand({ cmd: "set_timer", minutes });
  closeTimerDialog();
  showToast(`Timer set: ${minutes} min`);
});
document.getElementById("timer-cancel-btn").addEventListener("click", closeTimerDialog);

document.getElementById("settings-back-btn").addEventListener("click", () => showScreen("control"));
document.getElementById("settings-apply-btn").addEventListener("click", () => {
  sendCommand({ cmd: "set_settings", persist: false, settings: collectSettings() });
  document.getElementById("settings-status").textContent = "Applied for this session (lost on next reboot).";
});
document.getElementById("settings-save-btn").addEventListener("click", () => {
  sendCommand({ cmd: "set_settings", persist: true, settings: collectSettings() });
  document.getElementById("settings-status").textContent = "Saved as the new boot default.";
});
document.getElementById("change-connection-btn").addEventListener("click", openChangeConnection);
document.getElementById("connection-cancel-btn").addEventListener("click", closeChangeConnection);

setupPressButtons();
buildSettingsScreen();

// Best-effort safety net if the tab is backgrounded/closed mid-drive.
// Not guaranteed on iOS Safari (a killed background tab may not fire
// this) - the firmware's own angryTimeoutSec/menuTimeoutSec settings
// are the real backstop, same as for the desktop app.
document.addEventListener("pagehide", () => {
  if (controlActive) sendCommand({ cmd: "stop" });
});
document.addEventListener("visibilitychange", () => {
  if (document.hidden && controlActive) sendCommand({ cmd: "stop" });
});

if ("serviceWorker" in navigator) {
  window.addEventListener("load", () => {
    navigator.serviceWorker.register("sw.js").catch(() => {});
  });
}

// ---------- boot ----------
const loaded = tryLoadConfig();
if (loaded) {
  connectWithConfig(loaded);
  showScreen("home");
} else {
  showLoginForNewConnection();
}
