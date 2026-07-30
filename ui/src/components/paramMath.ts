/**
 * Pure math for the M1 rig params: normalized value (0..1) <-> real-world
 * units, plus the tiny bool convention shared by the on/off params. Kept
 * dependency-free and side-effect-free so it can be unit tested directly,
 * same spirit as ./knobMath.ts.
 *
 * Mapping rules come from the bridge contract (schema/bridge.schema.json +
 * app/source/WebviewBridge.h): the engine owns the authoritative real-world
 * value, but the UI needs the same math locally to render a live tooltip
 * while a knob is being dragged, before the echoed value round-trips back.
 *
 *   - dB params (gate threshold, amp EQ, output gain) are linear in
 *     normalized space over their range.
 *   - Cab cutoffs are log-mapped: hz = start * (end/start)^t.
 *   - Bools (gateOn, cabOn, namNormalize) are 0/1 in normalized space.
 */
import { clampNormalized } from "./knobMath";

export interface LinearDbRange {
  minDb: number;
  maxDb: number;
}

export const GATE_THRESHOLD_RANGE: LinearDbRange = { minDb: -90, maxDb: -20 };
export const AMP_DB_RANGE: LinearDbRange = { minDb: -12, maxDb: 12 };
export const OUTPUT_GAIN_RANGE: LinearDbRange = { minDb: -60, maxDb: 12 };

/** normalized value (0..1) -> real-world dB, linear over the range. */
export function normalizedToDb(value: number, range: LinearDbRange): number {
  return range.minDb + clampNormalized(value) * (range.maxDb - range.minDb);
}

/** real-world dB -> normalized value (0..1), clamped to the range. */
export function dbToNormalized(db: number, range: LinearDbRange): number {
  return clampNormalized((db - range.minDb) / (range.maxDb - range.minDb));
}

/** e.g. "+3.2 dB" / "-6.0 dB", for a knob's drag/hover tooltip. */
export function formatDb(value: number, range: LinearDbRange): string {
  const db = normalizedToDb(value, range);
  return `${db >= 0 ? "+" : ""}${db.toFixed(1)} dB`;
}

export const AMP_DB_DEFAULT_NORMALIZED = dbToNormalized(0, AMP_DB_RANGE);
export const OUTPUT_GAIN_DEFAULT_NORMALIZED = dbToNormalized(0, OUTPUT_GAIN_RANGE);
export const GATE_THRESHOLD_DEFAULT_DB = -40;
export const GATE_THRESHOLD_DEFAULT_NORMALIZED = dbToNormalized(GATE_THRESHOLD_DEFAULT_DB, GATE_THRESHOLD_RANGE);

export interface LogHzRange {
  startHz: number;
  endHz: number;
  defaultHz: number;
}

export const CAB_LOW_CUT_RANGE: LogHzRange = { startHz: 20, endHz: 400, defaultHz: 60 };
export const CAB_HIGH_CUT_RANGE: LogHzRange = { startHz: 2000, endHz: 20000, defaultHz: 8000 };

/** normalized value (0..1) -> Hz, log-mapped: start * (end/start)^t. */
export function normalizedToLogHz(value: number, range: LogHzRange): number {
  const t = clampNormalized(value);
  return range.startHz * Math.pow(range.endHz / range.startHz, t);
}

/** Hz (clamped to the range) -> normalized value (0..1), the inverse of normalizedToLogHz. */
export function logHzToNormalized(hz: number, range: LogHzRange): number {
  const clampedHz = Math.min(range.endHz, Math.max(range.startHz, hz));
  return clampNormalized(Math.log(clampedHz / range.startHz) / Math.log(range.endHz / range.startHz));
}

/** The normalized position of a log-mapped range's default Hz value. */
export function defaultNormalizedForLogHz(range: LogHzRange): number {
  return logHzToNormalized(range.defaultHz, range);
}

/** e.g. "60 Hz" below 1kHz, "8.0 kHz" at/above it -- sensible, no false precision. */
export function formatHz(hz: number): string {
  if (hz >= 1000) return `${(hz / 1000).toFixed(1)} kHz`;
  return `${Math.round(hz)} Hz`;
}

/** Formats a knob's normalized value straight to Hz/kHz for a log-mapped range. */
export function formatLogHz(value: number, range: LogHzRange): string {
  return formatHz(normalizedToLogHz(value, range));
}

/** Bools are 0/1 in normalized space; >=0.5 reads as "on" (mirrors the engine's own threshold). */
export function isParamOn(value: number): boolean {
  return value >= 0.5;
}

export function boolParamValue(on: boolean): number {
  return on ? 1 : 0;
}

/**
 * M2 floor pedal params (screamer/echoes/chamber): all nine continuous
 * params are plain 0..1 normalized floats end to end -- the engine
 * (sg::Screamer/StereoDelay/PlateReverb) owns the real-unit mapping
 * internally and never reports it back over the bridge, so the UI only
 * needs display-only math for a knob's drag/hover tooltip, same spirit as
 * the M1 math above but without a real engine value to be exactly right
 * about -- these are cosmetic approximations of the engine's own mapping,
 * not the source of truth.
 */
export interface LogMsRange {
  startMs: number;
  endMs: number;
}

/** Mirrors sg::StereoDelay's setTime() mapping (engine/include/sg/StereoDelay.h: 60ms..1200ms, log-mapped). */
export const ECHOES_TIME_RANGE: LogMsRange = { startMs: 60, endMs: 1200 };

/** normalized value (0..1) -> ms, log-mapped: start * (end/start)^t. */
export function normalizedToLogMs(value: number, range: LogMsRange): number {
  const t = clampNormalized(value);
  return range.startMs * Math.pow(range.endMs / range.startMs, t);
}

/** e.g. "620 ms" below 1s, "1.20 s" at/above it -- sensible, no false precision. */
export function formatMs(ms: number): string {
  if (ms >= 1000) return `${(ms / 1000).toFixed(2)} s`;
  return `${Math.round(ms)} ms`;
}

/** Formats a knob's normalized value straight to ms/s for a log-mapped time range. */
export function formatLogMs(value: number, range: LogMsRange): string {
  return formatMs(normalizedToLogMs(value, range));
}

/** Plain "0-10" pedal-knob dial reading for the other floor-pedal knobs (drive/tone/level/feedback/mix/decay) -- no real-world unit to report, just the familiar stompbox convention. */
export function format01AsTen(value: number): string {
  return (clampNormalized(value) * 10).toFixed(1);
}
