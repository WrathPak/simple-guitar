import type { CSSProperties } from "react";
import type { ParamHandle } from "../bridge";
import "./AmpDevice.css";
import { Knob } from "./Knob";
import { AMP_DB_DEFAULT_NORMALIZED, AMP_DB_RANGE, OUTPUT_GAIN_DEFAULT_NORMALIZED, OUTPUT_GAIN_RANGE, formatDb } from "./paramMath";

// Wide-shot decorative knob rotations (non-interactive, purely visual).
const WIDE_KNOB_ANGLES = [-30, 10, -15, 40];

export interface AmpKnobs {
  input: ParamHandle;
  bass: ParamHandle;
  mid: ParamHandle;
  treble: ParamHandle;
  presence: ParamHandle;
  output: ParamHandle;
}

const EQ_KNOB_DEFS: { key: keyof Omit<AmpKnobs, "output">; label: string }[] = [
  { key: "input", label: "Input" },
  { key: "bass", label: "Bass" },
  { key: "mid", label: "Mid" },
  { key: "treble", label: "Treble" },
  { key: "presence", label: "Presence" },
];

export interface AmpDeviceProps {
  focused: boolean;
  /** rigState.namModelName -- null renders a dimmed "no model". Only shown when focused isn't required: the wide plate reads it too. */
  namModelName?: string | null;
  /** Only used when focused. */
  normalizeOn?: boolean;
  onToggleNormalize?: () => void;
  onOpenModelOverlay?: () => void;
  knobs?: AmpKnobs;
  onFocus?: () => void;
}

/** NAM amp faceplate: decorative in the wide shot, fully interactive when focused. */
export function AmpDevice({
  focused,
  namModelName = null,
  normalizeOn = false,
  onToggleNormalize,
  onOpenModelOverlay,
  knobs,
  onFocus,
}: AmpDeviceProps) {
  const grille = <div className="amp-grille" />;
  const lamp = <div className="amp-lamp" />;
  const modelLabel = namModelName ?? "no model";
  const modelClass = `amp-id-model${namModelName ? "" : " amp-id-model--dim"}`;

  if (!focused) {
    return (
      <button type="button" className="amp-head amp-head--wide" onClick={onFocus} aria-label="Focus amplifier">
        {grille}
        <div className="amp-plate">
          <div className="amp-id">
            <div className="amp-id-mark">NAM</div>
            <div className={modelClass}>{modelLabel}</div>
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
          <div className={modelClass}>{modelLabel}</div>
          <button type="button" className="amp-id-change" onClick={onOpenModelOverlay}>
            change model ▾
          </button>
        </div>
        <div className="amp-knobs">
          {knobs &&
            EQ_KNOB_DEFS.map(({ key, label }) => (
              <Knob
                key={label}
                size={58}
                label={label}
                value={knobs[key].value}
                defaultValue={AMP_DB_DEFAULT_NORMALIZED}
                format={(v) => formatDb(v, AMP_DB_RANGE)}
                onChange={knobs[key].setValue}
              />
            ))}
          {knobs && (
            <Knob
              size={58}
              label="Output"
              value={knobs.output.value}
              defaultValue={OUTPUT_GAIN_DEFAULT_NORMALIZED}
              format={(v) => formatDb(v, OUTPUT_GAIN_RANGE)}
              onChange={knobs.output.setValue}
            />
          )}
        </div>
        <button type="button" className="amp-normalize" aria-pressed={normalizeOn} onClick={onToggleNormalize}>
          <span className="amp-normalize-led" aria-hidden="true" />
          <span className="amp-normalize-label">Normalize</span>
        </button>
        {lamp}
      </div>
    </div>
  );
}
