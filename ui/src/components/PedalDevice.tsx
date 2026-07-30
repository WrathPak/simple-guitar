import type { CSSProperties, KeyboardEvent, MouseEvent } from "react";
import "./PedalDevice.css";
import { Knob } from "./Knob";
import { angleToValue } from "./knobMath";
import type { PedalDef } from "./pedalDefs";

export interface PedalDeviceProps {
  pedal: PedalDef;
  bypassed: boolean;
  focused: boolean;
  onFocus?: () => void;
  onToggleBypass: () => void;
  /** Reports the IN/OUT jack elements as they mount, for cable geometry. Wide-row instances only. */
  onJackRef?: (which: "in" | "out", el: HTMLElement | null) => void;
  /** Shift+ArrowLeft/Right: swap this pedal with its neighbor in `direction`. */
  onSwap?: (direction: -1 | 1) => void;
}

/** Family-colored stompbox: decorative knobs in the wide shot, interactive when focused. */
export function PedalDevice({ pedal, bypassed, focused, onFocus, onToggleBypass, onJackRef, onSwap }: PedalDeviceProps) {
  const knobSize = focused ? 57 : 32;

  const handleFootswitchClick = (e: MouseEvent) => {
    e.stopPropagation();
    onToggleBypass();
  };

  const body = (
    <div
      className={[
        "pedal",
        `pedal--${pedal.family}`,
        focused ? "pedal--focused" : "pedal--wide",
        bypassed ? "pedal--bypassed" : "",
      ]
        .filter(Boolean)
        .join(" ")}
    >
      <div className="pedal-jacks" aria-hidden="true">
        <span className="pedal-jack pedal-jack--in" ref={(el) => onJackRef?.("in", el)} />
        <span className="pedal-jack pedal-jack--out" ref={(el) => onJackRef?.("out", el)} />
      </div>
      <div className="pedal-panel">
        {pedal.knobs.map((k) =>
          focused ? (
            <div key={k.label} className="pedal-knob-well">
              <Knob size={knobSize} label={k.label} defaultValue={angleToValue(k.angle)} />
            </div>
          ) : (
            <div key={k.label} className="pedal-knob-well">
              <div className="pedal-deco-knob" style={{ "--r": `${k.angle}deg` } as CSSProperties} />
              <div className="pedal-deco-label">{k.label}</div>
            </div>
          ),
        )}
      </div>
      <div className="pedal-name">{pedal.name}</div>
      <div className="pedal-foot">
        <div className="pedal-led" aria-hidden="true" />
        <button
          type="button"
          className="pedal-footswitch"
          aria-label={`${pedal.name} bypass`}
          aria-pressed={!bypassed}
          onClick={handleFootswitchClick}
        />
      </div>
    </div>
  );

  if (focused) return body;

  // A div (not a <button>) because the footswitch inside `body` is itself a
  // button — nested buttons are invalid HTML. Enter/Space still focus the gear.
  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.shiftKey && (e.key === "ArrowLeft" || e.key === "ArrowRight")) {
      e.preventDefault();
      onSwap?.(e.key === "ArrowRight" ? 1 : -1);
      return;
    }
    if (e.key !== "Enter" && e.key !== " ") return;
    e.preventDefault();
    onFocus?.();
  };

  return (
    <div
      className="pedal-trigger"
      role="button"
      tabIndex={0}
      onClick={onFocus}
      onKeyDown={handleKeyDown}
      aria-label={`Focus ${pedal.name}`}
    >
      {body}
    </div>
  );
}
