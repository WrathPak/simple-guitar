import type { ParamHandle } from "../bridge";
import "./CabDevice.css";
import { Knob } from "./Knob";
import { CAB_HIGH_CUT_RANGE, CAB_LOW_CUT_RANGE, defaultNormalizedForLogHz, formatLogHz } from "./paramMath";

export interface CabDeviceProps {
  focused: boolean;
  /** rigState.irName -- null renders a dimmed "no ir". Only meaningful when focused. */
  irName?: string | null;
  cabOn: boolean;
  onToggleCabOn?: () => void;
  onOpenIrOverlay?: () => void;
  lowCut?: ParamHandle;
  highCut?: ParamHandle;
  onFocus?: () => void;
  /** Where the last pedal's cable ends, tucked behind the cab's lower edge. Wide only. */
  ampAnchorRef?: React.MutableRefObject<HTMLElement | null>;
}

/** The cab: a separate focusable gear from the amp head it sits under -- IR select, Low/High cut, power. */
export function CabDevice({ focused, irName = null, cabOn, onToggleCabOn, onOpenIrOverlay, lowCut, highCut, onFocus, ampAnchorRef }: CabDeviceProps) {
  const offClass = cabOn ? "" : " cab--off";

  if (!focused) {
    return (
      <button type="button" className={`cab cab--wide${offClass}`} onClick={onFocus} aria-label="Focus cabinet">
        <div className="cab-cloth">
          <span className="cab-badge">Simple Guitar · 4×12 V30</span>
        </div>
        <span
          className="cab-cable-anchor"
          aria-hidden="true"
          ref={(el) => {
            if (ampAnchorRef) ampAnchorRef.current = el;
          }}
        />
      </button>
    );
  }

  const irLabel = irName ?? "no ir";
  const irClass = `cab-id-name${irName ? "" : " cab-id-name--dim"}`;

  return (
    <div className={`cab cab--focused${offClass}`}>
      <div className="cab-cloth" />
      <div className="cab-plate">
        <div className="cab-id">
          <div className="cab-id-mark">Cabinet</div>
          <div className={irClass}>{irLabel}</div>
          <button type="button" className="cab-id-change" onClick={onOpenIrOverlay}>
            change ir ▾
          </button>
        </div>
        <div className="cab-knobs">
          {lowCut && (
            <Knob
              label="Low Cut"
              size={58}
              value={lowCut.value}
              defaultValue={defaultNormalizedForLogHz(CAB_LOW_CUT_RANGE)}
              format={(v) => formatLogHz(v, CAB_LOW_CUT_RANGE)}
              onChange={lowCut.setValue}
            />
          )}
          {highCut && (
            <Knob
              label="High Cut"
              size={58}
              value={highCut.value}
              defaultValue={defaultNormalizedForLogHz(CAB_HIGH_CUT_RANGE)}
              format={(v) => formatLogHz(v, CAB_HIGH_CUT_RANGE)}
              onChange={highCut.setValue}
            />
          )}
        </div>
        <button type="button" className="cab-power" aria-pressed={cabOn} aria-label="Cab power" onClick={onToggleCabOn}>
          <span className="cab-power-led" aria-hidden="true" />
          <span className="cab-power-label">Power</span>
        </button>
      </div>
    </div>
  );
}
