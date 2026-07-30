import "./PedalRow.css";
import { PedalDevice } from "./PedalDevice";
import type { PedalDef } from "./pedalDefs";

export interface PedalRowState extends PedalDef {
  bypassed: boolean;
}

export interface PedalRowProps {
  pedals: PedalRowState[];
  onFocusPedal: (id: string) => void;
  onToggleBypass: (id: string) => void;
}

/** The floor: pedals in signal order, left to right, ending in a dim add spot. */
export function PedalRow({ pedals, onFocusPedal, onToggleBypass }: PedalRowProps) {
  return (
    <div className="pedal-row">
      {pedals.map((pedal) => (
        <PedalDevice
          key={pedal.id}
          pedal={pedal}
          bypassed={pedal.bypassed}
          focused={false}
          onFocus={() => onFocusPedal(pedal.id)}
          onToggleBypass={() => onToggleBypass(pedal.id)}
        />
      ))}
      <div className="pedal-row-add" aria-hidden="true">
        ＋
      </div>
    </div>
  );
}
