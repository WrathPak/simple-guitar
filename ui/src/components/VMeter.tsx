import "./VMeter.css";
import { clampDb, dbToFraction, formatDb } from "./meterMath";

export interface VMeterProps {
  /** Current input/output peak level in dBFS. */
  peakDb: number;
  /** Peak-hold tick level in dBFS. */
  holdDb: number;
}

/** 4x40px meter per DESIGN.md §3: gradient fill, peak-hold tick, dB readout beneath. */
export function VMeter({ peakDb, holdDb }: VMeterProps) {
  const fillPct = dbToFraction(peakDb) * 100;
  const holdPct = dbToFraction(holdDb) * 100;

  return (
    <div className="vmeter-col">
      <div className="vmeter" role="meter" aria-label="level" aria-valuemin={-60} aria-valuemax={6} aria-valuenow={clampDb(peakDb)}>
        <div className="vmeter-fill" style={{ height: `${fillPct}%` }} />
        <div className="vmeter-hold" style={{ bottom: `${holdPct}%` }} />
      </div>
      <div className="vmeter-value">{formatDb(peakDb)}</div>
    </div>
  );
}
