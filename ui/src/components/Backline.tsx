import "./Backline.css";
import { AmpDevice } from "./AmpDevice";
import { CabDevice } from "./CabDevice";

export interface BacklineProps {
  onFocusAmp: () => void;
  onFocusCab: () => void;
  /** Where the last pedal's cable ends, tucked behind the cab's lower edge. */
  ampAnchorRef?: React.MutableRefObject<HTMLElement | null>;
  namModelName: string | null;
  cabOn: boolean;
}

/** Amp head on its 4x12 cab, scaled down for the wide shot, cable running to the pedals. Both are separately focusable. */
export function Backline({ onFocusAmp, onFocusCab, ampAnchorRef, namModelName, cabOn }: BacklineProps) {
  return (
    <div className="backline">
      <div className="backline-stack">
        <AmpDevice focused={false} namModelName={namModelName} onFocus={onFocusAmp} />
        <CabDevice focused={false} cabOn={cabOn} onFocus={onFocusCab} ampAnchorRef={ampAnchorRef} />
      </div>
    </div>
  );
}
