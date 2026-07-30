import { onDevDirty } from "./devDirty";
import { getJuceBackend, isJuceHost } from "./juceClient";
import type { LibraryEntry, PresetResult, PresetsState } from "../../../schema/gen/ts/bridge";

/**
 * Wire contract for presets, matching app/source/WebviewBridge.h and
 * schema/bridge.schema.json (v3):
 *
 *   JS   -> native, channel "loadPreset":        { type: "loadPreset", path }
 *   JS   -> native, channel "savePresetAs":      { type: "savePresetAs", name }
 *   JS   -> native, channel "saveCurrentPreset": { type: "saveCurrentPreset" }
 *   JS   -> native, channel "nextPreset":        { type: "nextPreset" }
 *   JS   -> native, channel "prevPreset":        { type: "prevPreset" }
 *   JS   -> native, channel "requestPresets":    { type: "requestPresets" }
 *   native -> JS,   channel "presetsState": PresetsState (schema/gen/ts/bridge.ts)
 *   native -> JS,   channel "presetResult": PresetResult
 *
 * `presetsState` arrives unprompted on page load, after any preset op
 * resolves, in reply to "requestPresets", and whenever `dirty` flips
 * (throttled engine-side to ~4/s max). `presetResult` only follows
 * loadPreset/savePresetAs/saveCurrentPreset (not next/prev/requestPresets --
 * there's no inline failure surface for those in the UI), and is always
 * followed by a fresh presetsState.
 */

const LOAD_PRESET_CHANNEL = "loadPreset";
const SAVE_PRESET_AS_CHANNEL = "savePresetAs";
const SAVE_CURRENT_PRESET_CHANNEL = "saveCurrentPreset";
const NEXT_PRESET_CHANNEL = "nextPreset";
const PREV_PRESET_CHANNEL = "prevPreset";
const REQUEST_PRESETS_CHANNEL = "requestPresets";
const PRESETS_STATE_CHANNEL = "presetsState";
const PRESET_RESULT_CHANNEL = "presetResult";

function isPresetsState(data: unknown): data is PresetsState {
  return typeof data === "object" && data !== null && (data as { type?: unknown }).type === "presetsState";
}

function isPresetResult(data: unknown): data is PresetResult {
  return typeof data === "object" && data !== null && (data as { type?: unknown }).type === "presetResult";
}

function subscribeJucePresetsState(listener: (state: PresetsState) => void): () => void {
  const backend = getJuceBackend()!;
  const onBackendEvent = (data: unknown) => {
    if (isPresetsState(data)) listener(data);
  };
  backend.addEventListener(PRESETS_STATE_CHANNEL, onBackendEvent);
  return () => backend.removeEventListener(PRESETS_STATE_CHANNEL, onBackendEvent);
}

function subscribeJucePresetResult(listener: (result: PresetResult) => void): () => void {
  const backend = getJuceBackend()!;
  const onBackendEvent = (data: unknown) => {
    if (isPresetResult(data)) listener(data);
  };
  backend.addEventListener(PRESET_RESULT_CHANNEL, onBackendEvent);
  return () => backend.removeEventListener(PRESET_RESULT_CHANNEL, onBackendEvent);
}

// ---------------------------------------------------------------------------
// Dev fallback: no window.__JUCE__, so there's no engine/filesystem to save
// to. An in-memory preset store honoring all six messages, so the whole
// pill/overlay/save-as flow can be developed and tested in a plain browser.
// Dirty tracking mirrors the engine's rule (see PluginProcessor.h): any
// param change (sliderState.ts), model/IR load, or chain reorder (rig.ts)
// dirties the current preset -- those modules call markDevDirty() (see
// ./devDirty.ts) and this module listens for it.
// ---------------------------------------------------------------------------

const DEV_PRESETS_FOLDER = "Documents\\Simple Guitar\\Presets\\";

/** Same sanitizing rules as sg::sanitizePresetFilename (PresetStore.cpp): strip filesystem-reserved characters, trim. Empty result = invalid name. */
function devSanitizeFilename(rawName: string): string {
  return rawName.trim().replace(/[\\/:*?"<>|]/g, "_").trim();
}

let devPresets: LibraryEntry[] = [];
let devCurrent: LibraryEntry | null = null;
let devDirty = false;
const devPresetsStateListeners = new Set<(state: PresetsState) => void>();
const devPresetResultListeners = new Set<(result: PresetResult) => void>();

function buildDevPresetsState(): PresetsState {
  return {
    type: "presetsState",
    schemaVersion: 3,
    current: devCurrent,
    dirty: devDirty,
    presets: devPresets,
  };
}

function notifyDevPresetsState(): void {
  const state = buildDevPresetsState();
  for (const listener of devPresetsStateListeners) listener(state);
}

function notifyDevPresetResult(ok: boolean, message: string): void {
  const result: PresetResult = { type: "presetResult", ok, message };
  for (const listener of devPresetResultListeners) listener(result);
}

onDevDirty(() => {
  if (devDirty) return; // already dirty -- no-op, mirrors the engine's poll short-circuit.
  devDirty = true;
  notifyDevPresetsState();
});

function devSortPresets(): void {
  devPresets = [...devPresets].sort((a, b) => a.name.localeCompare(b.name));
}

function devSavePresetAs(rawName: string): void {
  const sanitized = devSanitizeFilename(rawName);
  if (sanitized === "") {
    notifyDevPresetResult(false, "name required");
    notifyDevPresetsState();
    return;
  }

  const path = `${DEV_PRESETS_FOLDER}${sanitized}.sgpreset`;
  const name = rawName.trim();
  devPresets = devPresets.filter((p) => p.path !== path);
  devPresets.push({ name, path });
  devSortPresets();
  devCurrent = { name, path };
  devDirty = false;

  notifyDevPresetResult(true, "saved");
  notifyDevPresetsState();
}

function devSaveCurrentPreset(): void {
  if (devCurrent === null) {
    notifyDevPresetResult(false, "no current preset -- use save as");
    notifyDevPresetsState();
    return;
  }
  devSavePresetAs(devCurrent.name);
}

function devLoadPreset(path: string): void {
  const entry = devPresets.find((p) => p.path === path);
  if (entry === undefined) {
    notifyDevPresetResult(false, "couldn't read preset (missing, corrupt, or wrong format)");
    notifyDevPresetsState();
    return;
  }

  devCurrent = entry;
  devDirty = false;
  notifyDevPresetResult(true, "loaded");
  notifyDevPresetsState();
}

function devStepPreset(direction: 1 | -1): void {
  if (devPresets.length === 0) {
    notifyDevPresetsState();
    return;
  }

  let index = direction === 1 ? 0 : devPresets.length - 1;
  if (devCurrent !== null) {
    const currentIndex = devPresets.findIndex((p) => p.path === devCurrent!.path);
    if (currentIndex !== -1) {
      index = (currentIndex + direction + devPresets.length) % devPresets.length;
    }
  }

  devCurrent = devPresets[index];
  devDirty = false;
  notifyDevPresetsState();
}

function subscribeDevPresetsState(listener: (state: PresetsState) => void): () => void {
  devPresetsStateListeners.add(listener);
  // Mirrors the engine pushing a presetsState immediately once the page loads.
  listener(buildDevPresetsState());
  return () => devPresetsStateListeners.delete(listener);
}

function subscribeDevPresetResult(listener: (result: PresetResult) => void): () => void {
  devPresetResultListeners.add(listener);
  return () => devPresetResultListeners.delete(listener);
}

/** Subscribe to presetsState pushes. Returns an unsubscribe function. */
export function subscribePresetsState(listener: (state: PresetsState) => void): () => void {
  return isJuceHost() ? subscribeJucePresetsState(listener) : subscribeDevPresetsState(listener);
}

/** Subscribe to presetResult events (one per loadPreset/savePresetAs/saveCurrentPreset request). Returns an unsubscribe function. */
export function subscribePresetResult(listener: (result: PresetResult) => void): () => void {
  return isJuceHost() ? subscribeJucePresetResult(listener) : subscribeDevPresetResult(listener);
}

/** Ask the engine to load the preset at `path` (must be a presetsState.presets[].path verbatim). */
export function sendLoadPreset(path: string): void {
  if (isJuceHost()) getJuceBackend()!.emitEvent(LOAD_PRESET_CHANNEL, { type: "loadPreset", path });
  else devLoadPreset(path);
}

/** Ask the engine to save the current rig state as a new (or overwritten) preset named `name`, making it current. */
export function sendSavePresetAs(name: string): void {
  if (isJuceHost()) getJuceBackend()!.emitEvent(SAVE_PRESET_AS_CHANNEL, { type: "savePresetAs", name });
  else devSavePresetAs(name);
}

/** Ask the engine to overwrite the current preset. Fails (presetResult ok:false) if there's no current preset. */
export function sendSaveCurrentPreset(): void {
  if (isJuceHost()) getJuceBackend()!.emitEvent(SAVE_CURRENT_PRESET_CHANNEL, { type: "saveCurrentPreset" });
  else devSaveCurrentPreset();
}

/** Ask the engine to load the next preset (sorted by name), wrapping around. No-op if there are no presets. */
export function sendNextPreset(): void {
  if (isJuceHost()) getJuceBackend()!.emitEvent(NEXT_PRESET_CHANNEL, { type: "nextPreset" });
  else devStepPreset(1);
}

/** Ask the engine to load the previous preset (sorted by name), wrapping around. No-op if there are no presets. */
export function sendPrevPreset(): void {
  if (isJuceHost()) getJuceBackend()!.emitEvent(PREV_PRESET_CHANNEL, { type: "prevPreset" });
  else devStepPreset(-1);
}

/** Ask for a fresh presetsState snapshot (e.g. when the presets overlay mounts). */
export function sendRequestPresets(): void {
  if (isJuceHost()) getJuceBackend()!.emitEvent(REQUEST_PRESETS_CHANNEL, { type: "requestPresets" });
  else notifyDevPresetsState();
}
