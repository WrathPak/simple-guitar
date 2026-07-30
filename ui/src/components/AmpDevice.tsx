import type { CSSProperties } from "react";
import "./AmpDevice.css";
import { Knob } from "./Knob";
import { angleToValue } from "./knobMath";

// Wide-shot decorative knob rotations (non-interactive, purely visual).
const WIDE_KNOB_ANGLES = [-30, 10, -15, 40];

// Focused faceplate knobs, in order. Output is bridge-wired; the rest are
// local-state-only stubs seeded from the same rotations shown in the wide shot.
const FOCUSED_KNOBS = [
  { label: "Input", angle: -30 },
  { label: "Bass", angle: 10 },
  { label: "Mid", angle: -15 },
  { label: "Treble", angle: 40 },
  { label: "Presence", angle: 25 },
];

export interface AmpDeviceProps {
  focused: boolean;
  /** Only used when focused: the OUTPUT knob is the one bridge-wired control. */
  outputValue?: number;
  onOutputChange?: (value: number) => void;
  onFocus?: () => void;
}

function formatGainDb(value: number): string {
  const db = -60 + value * 66;
  return `${db >= 0 ? "+" : ""}${db.toFixed(1)} dB`;
}

/** NAM amp faceplate: decorative in the wide shot, fully interactive when focused. */
export function AmpDevice({ focused, outputValue = 0.75, onOutputChange, onFocus }: AmpDeviceProps) {
  const grille = <div className="amp-grille" />;
  const lamp = <div className="amp-lamp" />;

  if (!focused) {
    return (
      <button type="button" className="amp-head amp-head--wide" onClick={onFocus} aria-label="Focus amplifier">
        {grille}
        <div className="amp-plate">
          <div className="amp-id">
            <div className="amp-id-mark">NAM</div>
            <div className="amp-id-model">MARK IIC+</div>
          </div>
          <div className="amp-deco-knobs">
            {WIDE_KNOB_ANGLES.map((angle, i) => (
              <div key={i} className="amp-deco-knob" style={{ "--r": `${angle}deg` } as CSSProperties} />
            ))}
          </div>
          {lamp}
        </div>
      </button>
    );
  }

  return (
    <div className="amp-head amp-head--focused">
      {grille}
      <div className="amp-plate">
        <div className="amp-id">
          <div className="amp-id-mark">NAM · Amplifier</div>
          <div className="amp-id-model">MARK IIC+</div>
          <button type="button" className="amp-id-change">
            change model ▾
          </button>
        </div>
        <div className="amp-knobs">
          {FOCUSED_KNOBS.map(({ label, angle }) => (
            <Knob key={label} size={58} label={label} defaultValue={angleToValue(angle)} />
          ))}
          <Knob
            size={58}
            label="Output"
            value={outputValue}
            defaultValue={0.75}
            format={formatGainDb}
            onChange={onOutputChange}
          />
        </div>
        {lamp}
      </div>
    </div>
  );
}
