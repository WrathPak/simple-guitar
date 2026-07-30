/**
 * Pure math for the level meters: dB clamping and dB <-> fill-fraction mapping.
 */

export const METER_MIN_DB = -60;
export const METER_MAX_DB = 6;

export function clampDb(db: number, min: number = METER_MIN_DB, max: number = METER_MAX_DB): number {
  if (Number.isNaN(db)) return min;
  return Math.min(max, Math.max(min, db));
}

/** Maps a dB value to a 0..1 fill fraction for the meter bar height. */
export function dbToFraction(db: number, min: number = METER_MIN_DB, max: number = METER_MAX_DB): number {
  const clamped = clampDb(db, min, max);
  return (clamped - min) / (max - min);
}

/** 9px dB readout text beneath the meter. */
export function formatDb(db: number): string {
  if (Number.isNaN(db) || db === Number.NEGATIVE_INFINITY || db <= METER_MIN_DB) return "-inf";
  const clamped = clampDb(db);
  const sign = clamped > 0 ? "+" : "";
  return `${sign}${clamped.toFixed(1)}`;
}

/**
 * Peak-hold tick update for one meter frame: hold jumps up instantly to a
 * new higher peak, then decays back down toward it at `decayPerTick` dB per
 * call when the incoming peak drops below the current hold.
 */
export function nextPeakHold(currentHold: number, incomingPeak: number, decayPerTick = 0.5): number {
  if (incomingPeak >= currentHold) return clampDb(incomingPeak);
  return clampDb(Math.max(incomingPeak, currentHold - decayPerTick));
}
