/**
 * Tiny pub/sub so the dev (no-`window.__JUCE__`) fallbacks in sliderState.ts
 * and rig.ts can tell presets.ts's dev preset store "something changed" --
 * mirrors the real engine's dirty rule (see PluginProcessor.h's class-level
 * comment): any param change, model/IR load, or chain reorder marks the
 * current preset dirty, until the next save/load clears it.
 *
 * Kept as its own leaf module (no imports) so sliderState.ts/rig.ts and
 * presets.ts can each depend on it without depending on each other.
 */

const listeners = new Set<() => void>();

/** Called by a dev fallback whenever it makes a change that should dirty the current preset. */
export function markDevDirty(): void {
  for (const listener of listeners) listener();
}

/** Subscribed once by presets.ts's dev preset store. Returns an unsubscribe function. */
export function onDevDirty(listener: () => void): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}
