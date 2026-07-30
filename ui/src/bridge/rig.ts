import { markDevDirty } from "./devDirty";
import { getJuceBackend, isJuceHost } from "./juceClient";
import { getSliderState } from "./sliderState";
import type {
  LibraryEntry,
  LoadResult,
  PedalTypeId,
  PluginEntry,
  PluginScanDone,
  PluginScanProgress,
  RigState,
  SlotState,
} from "../../../schema/gen/ts/bridge";

/**
 * Wire contract for the rig library (NAM models + cab IRs), the six floor
 * pedal slots, hosted third-party plugins, and their load/place/move flow,
 * matching app/source/WebviewBridge.h (schema v5):
 *
 *   JS   -> native, channel "loadNamModel":      { type: "loadNamModel", path }
 *   JS   -> native, channel "loadIr":            { type: "loadIr", path }
 *   JS   -> native, channel "requestRigState":   { type: "requestRigState" }
 *   JS   -> native, channel "setSlotType":       { type: "setSlotType", slot, pedalType }
 *   JS   -> native, channel "movePedal":         { type: "movePedal", from, to }
 *   JS   -> native, channel "requestPluginScan": { type: "requestPluginScan" }
 *   JS   -> native, channel "setSlotPlugin":     { type: "setSlotPlugin", slot, pluginId }
 *   JS   -> native, channel "openPluginEditor":  { type: "openPluginEditor", slot }
 *   native -> JS,   channel "rigState":           RigState (schema/gen/ts/bridge.ts)
 *   native -> JS,   channel "loadResult":         LoadResult
 *   native -> JS,   channel "pluginScanProgress": PluginScanProgress
 *   native -> JS,   channel "pluginScanDone":     PluginScanDone
 *
 * `rigState` arrives unprompted on page load and after every load request /
 * setSlotType / movePedal / setSlotPlugin resolves, after a scan finishes,
 * and also in direct reply to "requestRigState". `path` sent to
 * loadNamModel/loadIr must come from a `rigState.library` entry verbatim
 * and `pluginId` from a `rigState.plugins` entry verbatim -- the engine
 * rejects anything else. `slot`/`from`/`to` are slot indices 0..5 and
 * `pedalType` is a slot type choice 0..7 (0 = empty, 7 = hosted plugin);
 * anything out of range is silently rejected (no state change, no reply)
 * both by the engine and by this module's own dev fallback, so Room only
 * ever needs to derive its floor layout from `rigState.slots`, never from a
 * locally-optimistic copy. movePedal is an array move over all six slots
 * (remove at `from`, insert at `to`, the rest shift), and the slot params
 * (on + P1..P4) travel with their pedal.
 *
 * A type-7 slot's params are the same generic five: On is the bypass
 * footswitch and P1 is the plugin's wet/dry mix (P2..P4 are unused -- the
 * plugin's own controls live in its native editor, opened with
 * openPluginEditor).
 */

const LOAD_NAM_MODEL_CHANNEL = "loadNamModel";
const LOAD_IR_CHANNEL = "loadIr";
const REQUEST_RIG_STATE_CHANNEL = "requestRigState";
const SET_SLOT_TYPE_CHANNEL = "setSlotType";
const MOVE_PEDAL_CHANNEL = "movePedal";
const REQUEST_PLUGIN_SCAN_CHANNEL = "requestPluginScan";
const SET_SLOT_PLUGIN_CHANNEL = "setSlotPlugin";
const OPEN_PLUGIN_EDITOR_CHANNEL = "openPluginEditor";
const RIG_STATE_CHANNEL = "rigState";
const LOAD_RESULT_CHANNEL = "loadResult";
const PLUGIN_SCAN_PROGRESS_CHANNEL = "pluginScanProgress";
const PLUGIN_SCAN_DONE_CHANNEL = "pluginScanDone";

const SLOT_COUNT = 6;
const PEDAL_TYPE_COUNT = 8; // 0 empty + 6 built-in types + 7 hosted plugin.

/** The slot{N}Type choice value meaning "this slot hosts a third-party plugin". */
export const PLUGIN_PEDAL_TYPE = 7;

/** True if `slot` names one of the six floor slots. */
export function isValidSlotIndex(slot: number): boolean {
  return Number.isInteger(slot) && slot >= 0 && slot < SLOT_COUNT;
}

/** True if `pedalType` is a valid slot{N}Type choice (0 = empty .. 6 = shiver, 7 = hosted plugin). */
export function isValidPedalType(pedalType: number): pedalType is PedalTypeId {
  return Number.isInteger(pedalType) && pedalType >= 0 && pedalType < PEDAL_TYPE_COUNT;
}

function isRigState(data: unknown): data is RigState {
  return typeof data === "object" && data !== null && (data as { type?: unknown }).type === "rigState";
}

function isLoadResult(data: unknown): data is LoadResult {
  return typeof data === "object" && data !== null && (data as { type?: unknown }).type === "loadResult";
}

function isPluginScanProgress(data: unknown): data is PluginScanProgress {
  return typeof data === "object" && data !== null && (data as { type?: unknown }).type === "pluginScanProgress";
}

function isPluginScanDone(data: unknown): data is PluginScanDone {
  return typeof data === "object" && data !== null && (data as { type?: unknown }).type === "pluginScanDone";
}

function subscribeJuceRigState(listener: (state: RigState) => void): () => void {
  const backend = getJuceBackend()!;
  const onBackendEvent = (data: unknown) => {
    if (isRigState(data)) listener(data);
  };
  backend.addEventListener(RIG_STATE_CHANNEL, onBackendEvent);
  return () => backend.removeEventListener(RIG_STATE_CHANNEL, onBackendEvent);
}

function subscribeJuceLoadResult(listener: (result: LoadResult) => void): () => void {
  const backend = getJuceBackend()!;
  const onBackendEvent = (data: unknown) => {
    if (isLoadResult(data)) listener(data);
  };
  backend.addEventListener(LOAD_RESULT_CHANNEL, onBackendEvent);
  return () => backend.removeEventListener(LOAD_RESULT_CHANNEL, onBackendEvent);
}

function subscribeJucePluginScanProgress(listener: (progress: PluginScanProgress) => void): () => void {
  const backend = getJuceBackend()!;
  const onBackendEvent = (data: unknown) => {
    if (isPluginScanProgress(data)) listener(data);
  };
  backend.addEventListener(PLUGIN_SCAN_PROGRESS_CHANNEL, onBackendEvent);
  return () => backend.removeEventListener(PLUGIN_SCAN_PROGRESS_CHANNEL, onBackendEvent);
}

function subscribeJucePluginScanDone(listener: (done: PluginScanDone) => void): () => void {
  const backend = getJuceBackend()!;
  const onBackendEvent = (data: unknown) => {
    if (isPluginScanDone(data)) listener(data);
  };
  backend.addEventListener(PLUGIN_SCAN_DONE_CHANNEL, onBackendEvent);
  return () => backend.removeEventListener(PLUGIN_SCAN_DONE_CHANNEL, onBackendEvent);
}

// ---------------------------------------------------------------------------
// Dev fallback: no window.__JUCE__, so there's no engine to scan a real
// library folder, actually load a file, host a plugin, or own the slot
// state. Fake two models, two IRs and three installed plugins, honor
// setSlotType/movePedal/setSlotPlugin against an in-memory slots array
// (default: the classic screamer/echoes/chamber trio in slots 0-2), and
// simulate loads and plugin scans completing after a short delay -- enough
// to develop the whole palette/floor/picker/scan flow in a plain browser.
// ---------------------------------------------------------------------------

const DEV_MODELS_FOLDER = "Documents\\Simple Guitar\\Models\\";
const DEV_IRS_FOLDER = "Documents\\Simple Guitar\\IRs\\";

const DEV_LIBRARY: { models: LibraryEntry[]; irs: LibraryEntry[] } = {
  models: [
    { name: "Tweed Deluxe", path: `${DEV_MODELS_FOLDER}Tweed Deluxe.nam` },
    { name: "Plexi 800", path: `${DEV_MODELS_FOLDER}Plexi 800.nam` },
  ],
  irs: [
    { name: "4x12 V30", path: `${DEV_IRS_FOLDER}4x12 V30.wav` },
    { name: "2x12 Blue", path: `${DEV_IRS_FOLDER}2x12 Blue.wav` },
  ],
};

/** Stands in for a machine with a few VST3s installed. `id` is opaque, exactly as the engine's is. */
const DEV_PLUGINS: PluginEntry[] = [
  { id: "dev-vst3-crystal-verb", name: "Crystal Verb", vendor: "Northwind Audio" },
  { id: "dev-vst3-tape-echo", name: "Tape Echo", vendor: "Vallis DSP" },
  { id: "dev-vst3-tube-comp", name: "Tube Comp", vendor: "Vallis DSP" },
];

const DEV_LOAD_LATENCY_MS = 300;
const DEV_SCAN_TICK_MS = 140;
const DEV_MODEL_SAMPLE_RATE = 48000;

/** The classic trio in slots 0-2, matching the engine's own default state (every slot's On defaults true, flat). */
function devDefaultSlots(): SlotState[] {
  return [
    { type: 1, on: true },
    { type: 2, on: true },
    { type: 3, on: true },
    { type: 0, on: true },
    { type: 0, on: true },
    { type: 0, on: true },
  ];
}

let devNamModelName: string | null = null;
let devIrName: string | null = null;
let devSlots: SlotState[] = devDefaultSlots();
let devPlugins: PluginEntry[] = DEV_PLUGINS.slice();
let devScanRunning = false;
const devRigListeners = new Set<(state: RigState) => void>();
const devLoadResultListeners = new Set<(result: LoadResult) => void>();
const devScanProgressListeners = new Set<(progress: PluginScanProgress) => void>();
const devScanDoneListeners = new Set<(done: PluginScanDone) => void>();

function buildDevRigState(): RigState {
  return {
    type: "rigState",
    schemaVersion: 5,
    namModelName: devNamModelName,
    namModelSampleRate: devNamModelName ? DEV_MODEL_SAMPLE_RATE : 0,
    irName: devIrName,
    slots: devSlots.map((slot) => ({ ...slot })) as unknown as RigState["slots"],
    library: { models: DEV_LIBRARY.models, irs: DEV_LIBRARY.irs },
    plugins: devPlugins.map((plugin) => ({ ...plugin })),
  };
}

function notifyDevRigState(): void {
  const state = buildDevRigState();
  for (const listener of devRigListeners) listener(state);
}

function subscribeDevRigState(listener: (state: RigState) => void): () => void {
  devRigListeners.add(listener);
  // Mirrors the engine pushing a rigState immediately once the page loads.
  listener(buildDevRigState());
  return () => devRigListeners.delete(listener);
}

function subscribeDevLoadResult(listener: (result: LoadResult) => void): () => void {
  devLoadResultListeners.add(listener);
  return () => devLoadResultListeners.delete(listener);
}

function subscribeDevPluginScanProgress(listener: (progress: PluginScanProgress) => void): () => void {
  devScanProgressListeners.add(listener);
  return () => devScanProgressListeners.delete(listener);
}

function subscribeDevPluginScanDone(listener: (done: PluginScanDone) => void): () => void {
  devScanDoneListeners.add(listener);
  return () => devScanDoneListeners.delete(listener);
}

function devRequestRigState(): void {
  notifyDevRigState();
}

/** The dev param store handles for one slot, so slot edits keep the fake params coherent with the fake slots. */
function devSlotParams(slot: number) {
  return {
    on: getSliderState(`slot${slot}On`),
    ps: [1, 2, 3, 4].map((p) => getSliderState(`slot${slot}P${p}`)),
  };
}

function devSetSlotType(slot: number, pedalType: number): void {
  if (!isValidSlotIndex(slot) || !isValidPedalType(pedalType)) return; // silently rejected, same as the engine.
  if (devSlots[slot].type === pedalType) return; // no type change, no reset, no echo -- same as the engine.
  devSlots[slot] = { type: pedalType, on: true };
  // Mirrors the engine resetting a re-typed slot's params to the flat
  // defaults: a fresh pedal arrives on and with centered knobs.
  const params = devSlotParams(slot);
  params.on.setValue(1);
  for (const p of params.ps) p.setValue(0.5);
  markDevDirty();
  notifyDevRigState();
}

function devMovePedal(from: number, to: number): void {
  if (!isValidSlotIndex(from) || !isValidSlotIndex(to) || from === to) return;
  const nextSlots = devSlots.slice();
  const [movedSlot] = nextSlots.splice(from, 1);
  nextSlots.splice(to, 0, movedSlot);
  devSlots = nextSlots;

  // The slot params travel with their pedal, same as the engine: apply the
  // identical array move to the six (on, P1..P4) value groups.
  const groups = Array.from({ length: SLOT_COUNT }, (_, slot) => {
    const params = devSlotParams(slot);
    return { on: params.on.value, ps: params.ps.map((p) => p.value) };
  });
  const [movedGroup] = groups.splice(from, 1);
  groups.splice(to, 0, movedGroup);
  groups.forEach((group, slot) => {
    const params = devSlotParams(slot);
    params.on.setValue(group.on);
    group.ps.forEach((value, i) => params.ps[i].setValue(value));
  });

  markDevDirty();
  notifyDevRigState();
}

/**
 * Walks the fake plugin list one candidate per tick, emitting the same
 * progress/done pair the engine's out-of-process scanner does. Ignored while
 * a scan is already running, same as the engine.
 */
function devRequestPluginScan(): void {
  if (devScanRunning) return;
  devScanRunning = true;

  const candidates = DEV_PLUGINS;
  const step = (index: number) => {
    if (index < candidates.length) {
      const progress: PluginScanProgress = {
        type: "pluginScanProgress",
        done: index,
        total: candidates.length,
        currentName: candidates[index].name,
      };
      for (const listener of devScanProgressListeners) listener(progress);
      setTimeout(() => step(index + 1), DEV_SCAN_TICK_MS);
      return;
    }

    devScanRunning = false;
    devPlugins = candidates.slice();
    const done: PluginScanDone = { type: "pluginScanDone", found: devPlugins.length, failed: [] };
    for (const listener of devScanDoneListeners) listener(done);
    notifyDevRigState();
  };
  setTimeout(() => step(0), DEV_SCAN_TICK_MS);
}

function devSetSlotPlugin(slot: number, pluginId: string): void {
  if (!isValidSlotIndex(slot)) return;
  const entry = devPlugins.find((plugin) => plugin.id === pluginId);
  if (entry === undefined) return; // unknown id: silently rejected, same as the engine.

  devSlots[slot] = { type: PLUGIN_PEDAL_TYPE, on: true, pluginName: entry.name, pluginMissing: false };
  const params = devSlotParams(slot);
  params.on.setValue(1);
  for (const p of params.ps) p.setValue(0.5);
  markDevDirty();
  notifyDevRigState();

  // The engine builds the instance asynchronously and reports the outcome;
  // there's nothing to actually build here, so the fake always succeeds.
  setTimeout(() => {
    const result: LoadResult = { type: "loadResult", kind: "plugin", ok: true, message: `loaded ${entry.name}` };
    for (const listener of devLoadResultListeners) listener(result);
    notifyDevRigState();
  }, DEV_LOAD_LATENCY_MS);
}

function devLoad(kind: "nam" | "ir", path: string): void {
  const entries = kind === "nam" ? DEV_LIBRARY.models : DEV_LIBRARY.irs;
  const entry = entries.find((candidate) => candidate.path === path);

  setTimeout(() => {
    if (entry === undefined) {
      const result: LoadResult = { type: "loadResult", kind, ok: false, message: "not found in the managed library" };
      for (const listener of devLoadResultListeners) listener(result);
      notifyDevRigState();
      return;
    }

    if (kind === "nam") devNamModelName = entry.name;
    else devIrName = entry.name;

    markDevDirty();

    const result: LoadResult = { type: "loadResult", kind, ok: true, message: `loaded ${entry.name}` };
    for (const listener of devLoadResultListeners) listener(result);
    notifyDevRigState();
  }, DEV_LOAD_LATENCY_MS);
}

/** Subscribe to rigState pushes. Returns an unsubscribe function. */
export function subscribeRigState(listener: (state: RigState) => void): () => void {
  return isJuceHost() ? subscribeJuceRigState(listener) : subscribeDevRigState(listener);
}

/** Subscribe to loadResult events (one per loadNamModel/loadIr request, or per hosted-plugin instance build). Returns an unsubscribe function. */
export function subscribeLoadResult(listener: (result: LoadResult) => void): () => void {
  return isJuceHost() ? subscribeJuceLoadResult(listener) : subscribeDevLoadResult(listener);
}

/** Subscribe to plugin-scan progress ticks (many per scan). Returns an unsubscribe function. */
export function subscribePluginScanProgress(listener: (progress: PluginScanProgress) => void): () => void {
  return isJuceHost() ? subscribeJucePluginScanProgress(listener) : subscribeDevPluginScanProgress(listener);
}

/** Subscribe to the single pluginScanDone that ends each scan. Returns an unsubscribe function. */
export function subscribePluginScanDone(listener: (done: PluginScanDone) => void): () => void {
  return isJuceHost() ? subscribeJucePluginScanDone(listener) : subscribeDevPluginScanDone(listener);
}

/** Ask the engine to load a .nam model. `path` must be a rigState.library.models[].path verbatim. */
export function sendLoadNamModel(path: string): void {
  if (isJuceHost()) getJuceBackend()!.emitEvent(LOAD_NAM_MODEL_CHANNEL, { type: "loadNamModel", path });
  else devLoad("nam", path);
}

/** Ask the engine to load a cab IR. `path` must be a rigState.library.irs[].path verbatim. */
export function sendLoadIr(path: string): void {
  if (isJuceHost()) getJuceBackend()!.emitEvent(LOAD_IR_CHANNEL, { type: "loadIr", path });
  else devLoad("ir", path);
}

/** Ask for a fresh rigState snapshot (e.g. when a library overlay mounts). */
export function sendRequestRigState(): void {
  if (isJuceHost()) getJuceBackend()!.emitEvent(REQUEST_RIG_STATE_CHANNEL, { type: "requestRigState" });
  else devRequestRigState();
}

/**
 * Put a pedal type into a slot (palette pick), or empty it (pedalType 0 =
 * remove). Out-of-range values are silently dropped (not even sent to the
 * engine) rather than emitted and rejected server-side, since there's
 * nothing useful to do with the rejection either way. The floor updates
 * once the engine echoes the change on the next rigState.
 */
export function sendSetSlotType(slot: number, pedalType: number): void {
  if (!isValidSlotIndex(slot) || !isValidPedalType(pedalType)) return;
  if (isJuceHost()) getJuceBackend()!.emitEvent(SET_SLOT_TYPE_CHANNEL, { type: "setSlotType", slot, pedalType });
  else devSetSlotType(slot, pedalType);
}

/**
 * Commits a reorder (drag drop or keyboard swap): move the pedal in slot
 * `from` to slot `to`, shifting the slots between them. Same silent-drop
 * rule for out-of-range indices as sendSetSlotType.
 */
export function sendMovePedal(from: number, to: number): void {
  if (!isValidSlotIndex(from) || !isValidSlotIndex(to) || from === to) return;
  if (isJuceHost()) getJuceBackend()!.emitEvent(MOVE_PEDAL_CHANNEL, { type: "movePedal", from, to });
  else devMovePedal(from, to);
}

/**
 * Start a scan of the standard plugin locations. Never runs on its own --
 * only when the user asks for it. Progress arrives on
 * subscribePluginScanProgress, the summary on subscribePluginScanDone, and
 * the refreshed `rigState.plugins` right after.
 */
export function sendRequestPluginScan(): void {
  if (isJuceHost()) getJuceBackend()!.emitEvent(REQUEST_PLUGIN_SCAN_CHANNEL, { type: "requestPluginScan" });
  else devRequestPluginScan();
}

/**
 * Put a scanned plugin into a slot. `pluginId` must be a
 * `rigState.plugins` entry's id verbatim. Send this after
 * sendSetSlotType(slot, PLUGIN_PEDAL_TYPE): the slot becomes a type-7 pedal
 * whose On is bypass and P1 is wet/dry mix. The build resolves
 * asynchronously -- a loadResult with kind "plugin" reports the outcome.
 */
export function sendSetSlotPlugin(slot: number, pluginId: string): void {
  if (!isValidSlotIndex(slot) || pluginId === "") return;
  if (isJuceHost()) getJuceBackend()!.emitEvent(SET_SLOT_PLUGIN_CHANNEL, { type: "setSlotPlugin", slot, pluginId });
  else devSetSlotPlugin(slot, pluginId);
}

/**
 * Open (or refocus) the hosted plugin's own editor -- the one sanctioned
 * native window. Silently ignored by the engine unless the slot holds a
 * live plugin instance; the dev fallback has no plugin to open, so outside
 * the JUCE host this does nothing.
 */
export function sendOpenPluginEditor(slot: number): void {
  if (!isValidSlotIndex(slot)) return;
  if (isJuceHost()) getJuceBackend()!.emitEvent(OPEN_PLUGIN_EDITOR_CHANNEL, { type: "openPluginEditor", slot });
}
