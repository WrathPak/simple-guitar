/**
 * Low-level transport to the JUCE 8 WebView host.
 *
 * ---------------------------------------------------------------------------
 * Background (read this before touching relay/event names):
 *
 * JUCE 8 ships an official JS frontend module alongside `WebBrowserComponent`
 * at `modules/juce_gui_extra/native/javascript/` in the JUCE repo (package
 * name `juce-framework-frontend`, see its `package.json`). It is NOT published
 * to the public npm registry (there is an open JUCE forum feature request for
 * that — "FR: Official NPM package for WebView UIs") — it ships only inside a
 * JUCE checkout and is normally consumed as a `file:` dependency once JUCE is
 * vendored into the app build (see JUCE's `examples/Plugins/WebViewPluginDemo.h`
 * / `.../WebViewPluginDemo.cpp` for the reference C++↔React integration this
 * bridge is modeled on).
 *
 * That library's documented public surface (`getSliderState`, `getToggleState`,
 * `getComboBoxState`, `getNativeFunction`, `getBackendResourceAddress`) is a
 * thin ergonomic wrapper around two lower-level primitives that JUCE injects
 * as a global before any frontend script runs:
 *
 *   window.__JUCE__.backend.addEventListener(eventId, listener)
 *   window.__JUCE__.backend.emitEvent(eventId, data)
 *
 * `app/` hasn't vendored JUCE yet (that's a different workstream), so the
 * real npm package isn't installable from `ui/` right now and a static
 * `import ... from "juce-framework-frontend"` would break `npm run build`.
 * This module therefore talks to `window.__JUCE__.backend` directly using
 * those same two confirmed primitives, and defines our own small wire
 * contract on top (documented per-function below, and restated in the M0
 * handoff report) for what the C++ side needs to emit/listen for. Once JUCE
 * is vendored and `juce-framework-frontend` becomes a real dependency, only
 * this file needs to change — everything importing from `./index.ts` is
 * unaffected.
 * ---------------------------------------------------------------------------
 */

export interface JuceBackend {
  addEventListener(eventId: string, listener: (data: unknown) => void): void;
  removeEventListener(eventId: string, listener: (data: unknown) => void): void;
  emitEvent(eventId: string, data: unknown): void;
}

export interface JuceGlobal {
  backend: JuceBackend;
  initialisationData?: Record<string, unknown>;
}

declare global {
  interface Window {
    __JUCE__?: JuceGlobal;
  }
}

export function isJuceHost(): boolean {
  return typeof window !== "undefined" && window.__JUCE__ != null;
}

export function getJuceBackend(): JuceBackend | undefined {
  if (typeof window === "undefined" || window.__JUCE__ == null) return undefined;
  return window.__JUCE__.backend;
}
