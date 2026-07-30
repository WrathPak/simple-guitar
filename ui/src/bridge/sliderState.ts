import { getJuceBackend, isJuceHost } from "./juceClient";
import type { SliderStateHandle } from "./types";

/**
 * Wire contract for a single normalized slider parameter, channel = the
 * relay name (e.g. "outputGain"):
 *
 *   JS  -> native: emitEvent(name, { type: "valueChanged", value })
 *   JS  -> native: emitEvent(name, { type: "gestureStart" | "gestureEnd" })
 *   native -> JS:  addEventListener(name, ({ type: "valueChanged", value }) => ...)
 *
 * The C++ side is expected to push an initial `valueChanged` event for every
 * registered relay once the page finishes loading (mirrors JUCE's
 * WebSliderRelay construction-time sync), so the frontend never needs a
 * separate "request current value" round trip.
 */
interface ValueChangedMessage {
  type: "valueChanged";
  value: number;
}

function isValueChangedMessage(data: unknown): data is ValueChangedMessage {
  return (
    typeof data === "object" &&
    data !== null &&
    (data as { type?: unknown }).type === "valueChanged" &&
    typeof (data as { value?: unknown }).value === "number"
  );
}

const devValues = new Map<string, number>();
const devListeners = new Map<string, Set<(value: number) => void>>();

function clamp01(value: number): number {
  if (Number.isNaN(value)) return 0;
  return Math.min(1, Math.max(0, value));
}

function getDevSliderState(name: string, initialValue: number): SliderStateHandle {
  if (!devValues.has(name)) devValues.set(name, clamp01(initialValue));
  if (!devListeners.has(name)) devListeners.set(name, new Set());

  const notify = () => {
    const value = devValues.get(name)!;
    for (const listener of devListeners.get(name)!) listener(value);
  };

  return {
    get value() {
      return devValues.get(name)!;
    },
    setValue(next: number) {
      devValues.set(name, clamp01(next));
      notify();
    },
    beginGesture() {
      /* no-op in dev fallback */
    },
    endGesture() {
      /* no-op in dev fallback */
    },
    subscribe(listener) {
      devListeners.get(name)!.add(listener);
      return () => devListeners.get(name)?.delete(listener);
    },
  };
}

function getJuceSliderState(name: string, initialValue: number): SliderStateHandle {
  const backend = getJuceBackend()!;
  let currentValue = clamp01(initialValue);
  const listeners = new Set<(value: number) => void>();

  const onBackendEvent = (data: unknown) => {
    if (!isValueChangedMessage(data)) return;
    currentValue = clamp01(data.value);
    for (const listener of listeners) listener(currentValue);
  };
  backend.addEventListener(name, onBackendEvent);

  return {
    get value() {
      return currentValue;
    },
    setValue(next: number) {
      currentValue = clamp01(next);
      backend.emitEvent(name, { type: "valueChanged", value: currentValue });
      for (const listener of listeners) listener(currentValue);
    },
    beginGesture() {
      backend.emitEvent(name, { type: "gestureStart" });
    },
    endGesture() {
      backend.emitEvent(name, { type: "gestureEnd" });
    },
    subscribe(listener) {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
  };
}

/**
 * Get a live handle to a normalized (0..1) slider parameter identified by
 * `name` (the relay name the C++ side registers, e.g. "outputGain"). Falls
 * back to a fully functional in-memory implementation when `window.__JUCE__`
 * is absent (plain-browser / `vite dev` development).
 */
export function getSliderState(name: string, initialValue = 0.5): SliderStateHandle {
  return isJuceHost() ? getJuceSliderState(name, initialValue) : getDevSliderState(name, initialValue);
}
