import type { JuceBackend } from "../bridge/juceClient";

/**
 * A fake window.__JUCE__.backend so a param/message round trip through the
 * real bridge contract (see bridge/sliderState.ts, bridge/rig.ts) can be
 * tested in isolation, without touching the dev-fallback module singletons
 * other tests rely on staying untouched.
 */
export function createFakeJuceBackend() {
  const listeners = new Map<string, Set<(data: unknown) => void>>();
  const emitted: { id: string; data: unknown }[] = [];
  const backend: JuceBackend = {
    addEventListener(id, listener) {
      if (!listeners.has(id)) listeners.set(id, new Set());
      listeners.get(id)!.add(listener);
    },
    removeEventListener(id, listener) {
      listeners.get(id)?.delete(listener);
    },
    emitEvent(id, data) {
      emitted.push({ id, data });
    },
  };
  return {
    backend,
    emitted,
    trigger(id: string, data: unknown) {
      listeners.get(id)?.forEach((listener) => listener(data));
    },
  };
}
