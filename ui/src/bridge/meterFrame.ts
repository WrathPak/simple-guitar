import { getJuceBackend, isJuceHost } from "./juceClient";
import type { MeterFrame } from "./types";

/**
 * Wire contract for meter data, channel = "meterFrame":
 *
 *   native -> JS: emitEvent("meterFrame", { inPeakDb: number, outPeakDb: number })
 *
 * The C++ side should emit this from a UI-rate timer (~30-60Hz), guarded by
 * something like `emitEventIfBrowserIsVisible` (as JUCE's WebViewPluginDemo
 * does for its spectrum data) so it doesn't do work while the stage/window
 * isn't visible. There is no JS -> native direction for this channel.
 */
const METER_EVENT_ID = "meterFrame";

function isMeterFrame(data: unknown): data is MeterFrame {
  return (
    typeof data === "object" &&
    data !== null &&
    typeof (data as { inPeakDb?: unknown }).inPeakDb === "number" &&
    typeof (data as { outPeakDb?: unknown }).outPeakDb === "number"
  );
}

function subscribeJuceMeterFrame(listener: (frame: MeterFrame) => void): () => void {
  const backend = getJuceBackend()!;
  const onBackendEvent = (data: unknown) => {
    if (isMeterFrame(data)) listener(data);
  };
  backend.addEventListener(METER_EVENT_ID, onBackendEvent);
  return () => backend.removeEventListener(METER_EVENT_ID, onBackendEvent);
}

const FAKE_METER_INTERVAL_MS = 1000 / 30;
const FAKE_METER_MIN_DB = -60;
const FAKE_METER_IDLE_DB = -42;

/** Dev fallback: fake input/output meter data so the app is developable in a plain browser. */
function subscribeDevMeterFrame(listener: (frame: MeterFrame) => void): () => void {
  let t = 0;
  const id = setInterval(() => {
    t += FAKE_METER_INTERVAL_MS;
    // Slow, gentle wandering "playing" signal so the meters look alive but not manic.
    const wobble = Math.sin(t / 480) * 10 + Math.sin(t / 137) * 4;
    const inPeakDb = Math.max(FAKE_METER_MIN_DB, FAKE_METER_IDLE_DB + wobble + 6);
    const outPeakDb = Math.max(FAKE_METER_MIN_DB, FAKE_METER_IDLE_DB + wobble * 1.3 + 3);
    listener({ inPeakDb, outPeakDb });
  }, FAKE_METER_INTERVAL_MS);
  return () => clearInterval(id);
}

/**
 * Subscribe to "meterFrame" events carrying {inPeakDb, outPeakDb}. Returns an
 * unsubscribe function. Falls back to animated fake data when
 * `window.__JUCE__` is absent.
 */
export function subscribeMeterFrame(listener: (frame: MeterFrame) => void): () => void {
  return isJuceHost() ? subscribeJuceMeterFrame(listener) : subscribeDevMeterFrame(listener);
}
