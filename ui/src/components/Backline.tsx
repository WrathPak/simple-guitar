import "./Backline.css";
import { AmpDevice } from "./AmpDevice";

export interface BacklineProps {
  onFocusAmp: () => void;
}

/** Amp head on its 4x12 cab, scaled down for the wide shot, cable running to the pedals. */
export function Backline({ onFocusAmp }: BacklineProps) {
  return (
    <div className="backline">
      <div className="backline-stack">
        <AmpDevice focused={false} onFocus={onFocusAmp} />
        <div className="backline-cab">
          <div className="backline-cloth">
            <span className="backline-badge">Simple Guitar · 4×12 V30</span>
          </div>
        </div>
      </div>
    </div>
  );
}
