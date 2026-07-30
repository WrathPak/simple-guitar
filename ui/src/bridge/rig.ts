import { getJuceBackend, isJuceHost } from "./juceClient";
import type { ChainOrder, LibraryEntry, LoadResult, PedalId, RigState } from "../../../schema/gen/ts/bridge";

/**
 * Wire contract for the rig library (NAM models + cab IRs), the floor pedal
 * chain order, and their load/select/reorder flow, matching
 * app/source/WebviewBridge.h:
 *
 *   JS   -> native, channel "loadNamModel":    { type: "loadNamModel", path }
 *   JS   -> native, channel "loadIr":          { type: "loadIr", path }
 *   JS   -> native, channel "requestRigState": { type: "requestRigState" }
 *   JS   -> native, channel "setChainOrder":   { type: "setChainOrder", order }
 *   native -> JS,   channel "rigState":  RigState (schema/gen/ts/bridge.ts)
 *   native -> JS,   channel "loadResult": LoadResult
 *
 * `rigState` arrives unprompted on page load and after every load request or
 * setChainOrder resolves (success or failure/rejection), and also in direct
 * reply to "requestRigState". `path` sent to loadNamModel/loadIr must come
 * from a `rigState.library` entry verbatim -- the engine rejects anything
 * else. `order` sent to setChainOrder must be exactly a permutation of the
 * three PedalIds -- an invalid order is silently rejected (no state change,
 * no reply) both by the engine and by this module's own dev fallback, so
 * Room only ever needs to derive its floor layout from `rigState.chainOrder`,
 * never from a locally-optimistic order.
 */

const LOAD_NAM_MODEL_CHANNEL = "loadNamModel";
const LOAD_IR_CHANNEL = "loadIr";
const REQUEST_RIG_STATE_CHANNEL = "requestRigState";
const SET_CHAIN_ORDER_CHANNEL = "setChainOrder";
const RIG_STATE_CHANNEL = "rigState";
const LOAD_RESULT_CHANNEL = "loadResult";

const PEDAL_IDS: readonly PedalId[] = ["screamer", "echoes", "chamber"];

/** True if `order` is exactly a permutation of the three pedal ids -- the same validation the engine performs (see app/source/ChainOrder.h's isValidPedalOrder). */
export function isValidChainOrder(order: readonly string[]): order is ChainOrder {
  if (order.length !== PEDAL_IDS.length) return false;
  const seen = new Set<string>();
  for (const id of order) {
    if (!(PEDAL_IDS as readonly string[]).includes(id)) return false;
    if (seen.has(id)) return false;
    seen.add(id);
  }
  return true;
}

function isRigState(data: unknown): data is RigState {
  return typeof data === "object" && data !== null && (data as { type?: unknown }).type === "rigState";
}

function isLoadResult(data: unknown): data is LoadResult {
  return typeof data === "object" && data !== null && (data as { type?: unknown }).type === "loadResult";
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

// ---------------------------------------------------------------------------
// Dev fallback: no window.__JUCE__, so there's no engine to scan a real
// library folder or actually load a file. Fake two models and two IRs, and
// simulate a load completing (successfully, matching a verbatim library
// path) after a short delay -- enough to develop the whole picker/loading/
// error-copy flow in a plain browser.
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

const DEV_LOAD_LATENCY_MS = 300;
const DEV_MODEL_SAMPLE_RATE = 48000;

const DEV_DEFAULT_CHAIN_ORDER: ChainOrder = ["screamer", "echoes", "chamber"];

let devNamModelName: string | null = null;
let devIrName: string | null = null;
let devChainOrder: ChainOrder = DEV_DEFAULT_CHAIN_ORDER;
const devRigListeners = new Set<(state: RigState) => void>();
const devLoadResultListeners = new Set<(result: LoadResult) => void>();

function buildDevRigState(): RigState {
  return {
    type: "rigState",
    schemaVersion: 2,
    namModelName: devNamModelName,
    namModelSampleRate: devNamModelName ? DEV_MODEL_SAMPLE_RATE : 0,
    irName: devIrName,
    chainOrder: devChainOrder,
    library: { models: DEV_LIBRARY.models, irs: DEV_LIBRARY.irs },
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

function devRequestRigState(): void {
  notifyDevRigState();
}

function devSetChainOrder(order: readonly string[]): void {
  if (!isValidChainOrder(order)) return; // silently rejected, same as the engine.
  devChainOrder = order;
  notifyDevRigState();
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

    const result: LoadResult = { type: "loadResult", kind, ok: true, message: `loaded ${entry.name}` };
    for (const listener of devLoadResultListeners) listener(result);
    notifyDevRigState();
  }, DEV_LOAD_LATENCY_MS);
}

/** Subscribe to rigState pushes. Returns an unsubscribe function. */
export function subscribeRigState(listener: (state: RigState) => void): () => void {
  return isJuceHost() ? subscribeJuceRigState(listener) : subscribeDevRigState(listener);
}

/** Subscribe to loadResult events (one per loadNamModel/loadIr request). Returns an unsubscribe function. */
export function subscribeLoadResult(listener: (result: LoadResult) => void): () => void {
  return isJuceHost() ? subscribeJuceLoadResult(listener) : subscribeDevLoadResult(listener);
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
 * Commits a new floor pedal chain order (e.g. after a drag-reorder or
 * keyboard swap gesture). `order` must be exactly a permutation of the three
 * pedal ids -- an invalid order is silently dropped (not even sent to the
 * engine) rather than emitted and rejected server-side, since there's
 * nothing useful to do with the rejection either way.
 */
export function sendSetChainOrder(order: readonly string[]): void {
  if (!isValidChainOrder(order)) return;
  if (isJuceHost()) getJuceBackend()!.emitEvent(SET_CHAIN_ORDER_CHANNEL, { type: "setChainOrder", order });
  else devSetChainOrder(order);
}
