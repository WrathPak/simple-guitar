import "./Backline.css";
import { AmpDevice } from "./AmpDevice";

export interface BacklineProps {
  onFocusAmp: () => void;
  /** Where the last pedal's cable ends, tucked behind the amp's lower edge. */
  ampAnchorRef?: React.MutableRefObject<HTMLElement | null>;
}

/** Amp head on its 4x12 cab, scaled down for the wide shot, cable running to the pedals. */
export function Backline({ onFocusAmp, ampAnchorRef }: BacklineProps) {
  return (
    <div className="backline">
      <div className="backline-stack">
        <AmpDevice focused={false} onFocus={onFocusAmp} />
        <div className="backline-cab">
          <div className="backline-cloth">
            <span className="backline-badge">Simple Guitar · 4×12 V30</span>
          </div>
          <span
            className="backline-cable-anchor"
            aria-hidden="true"
            ref={(el) => {
              if (ampAnchorRef) ampAnchorRef.current = el;
            }}
          />
        </div>
      </div>
    </div>
  );
}
