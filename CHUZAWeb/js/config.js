// Ported from CHUZAControls/net.py's try_load_config()/save_config().
// localStorage stands in for config.json - same contract: return null
// on anything wrong so the caller falls back to the Login screen,
// merge saved fields over the defaults so callers never need the full
// schema, and never ship real credentials in the deployed site (nothing
// is written here until the user submits the Login form).
import { DEFAULT_CONFIG, REQUIRED_KEYS } from "./schema.js";

const STORAGE_KEY = "chuza.config";

export function tryLoadConfig() {
  const raw = localStorage.getItem(STORAGE_KEY);
  if (!raw) return null;
  let data;
  try {
    data = JSON.parse(raw);
  } catch {
    return null;
  }
  for (const key of REQUIRED_KEYS) {
    const value = data[key];
    if (!value || (typeof value === "string" && value.includes("REPLACE_ME"))) {
      return null;
    }
  }
  return { ...DEFAULT_CONFIG, ...data };
}

export function saveConfig(fields) {
  const merged = { ...DEFAULT_CONFIG, ...fields };
  localStorage.setItem(STORAGE_KEY, JSON.stringify(merged));
  return merged;
}
