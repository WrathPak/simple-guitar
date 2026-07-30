import "./EdgeMeter.css";
import { clampDb, dbToFraction } from "./meterMath";

export interface EdgeMeterProps {
  /** Current input/output peak level in dBFS. */
  peakDb: number;
  /** Peak-hold tick level in dBFS. */
  holdDb: number;
  /** Which window edge this meter hugs. */
  side: "l" | "r";
  label: string;
}

/** Hairline 3x130 meter hugging a window edge. No value text at rest. */
export function EdgeMeter({ peakDb, holdDb, side, label }: EdgeMeterProps) {
  const fillPct = dbToFraction(peakDb) * 100;
  const holdPct = dbToFraction(holdDb) * 100;

  return (
    <div
      className={`edge-meter edge-meter--${side}`}
      role="meter"
      aria-label={label}
      aria-valuemin={-60}
      aria-valuemax={6}
      aria-valuenow={clampDb(peakDb)}
    >
      <div className="edge-meter-fill" style={{ height: `${fillPct}%` }} />
      <div className="edge-meter-hold" style={{ bottom: `${holdPct}%` }} />
    </div>
  );
}
