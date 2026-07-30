import type { ParamHandle } from "../bridge";
import "./GateOverlay.css";
import { Knob } from "./Knob";
import { Overlay } from "./Overlay";
import { GATE_THRESHOLD_DEFAULT_NORMALIZED, GATE_THRESHOLD_RANGE, formatDb, isParamOn } from "./paramMath";

export interface GateOverlayProps {
  gateOn: ParamHandle;
  gateThreshold: ParamHandle;
  onClose: () => void;
}

/** Gate: on/off + a threshold knob, tucked behind the ⋯ menu rather than on the floor (it's rack processing, not a pedal). */
export function GateOverlay({ gateOn, gateThreshold, onClose }: GateOverlayProps) {
  const on = isParamOn(gateOn.value);

  return (
    <Overlay title="Gate" onClose={onClose}>
      <div className="gate-overlay-body">
        <button type="button" className="gate-toggle" aria-pressed={on} onClick={() => gateOn.setValue(on ? 0 : 1)}>
          <span className="gate-toggle-led" aria-hidden="true" />
          <span className="gate-toggle-label">Gate</span>
        </button>
        <Knob
          label="Threshold"
          size={58}
          value={gateThreshold.value}
          defaultValue={GATE_THRESHOLD_DEFAULT_NORMALIZED}
          format={(v) => formatDb(v, GATE_THRESHOLD_RANGE)}
          onChange={gateThreshold.setValue}
        />
      </div>
    </Overlay>
  );
}
