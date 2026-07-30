import type { CSSProperties, KeyboardEvent, MouseEvent } from "react";
import type { ParamHandle } from "../bridge";
import "./PedalDevice.css";
import { Knob } from "./Knob";
import type { PedalTypeDef } from "./pedalDefs";

export interface PedalDeviceProps {
  pedal: PedalTypeDef;
  bypassed: boolean;
  focused: boolean;
  onFocus?: () => void;
  onToggleBypass: () => void;
  /** Reports the IN/OUT jack elements as they mount, for cable geometry. Wide-row instances only. */
  onJackRef?: (which: "in" | "out", el: HTMLElement | null) => void;
  /** Shift+ArrowLeft/Right: swap this pedal with its neighbor in `direction`. */
  onSwap?: (direction: -1 | 1) => void;
  /** Live param handles, same order/length as pedal.knobs (3 or 4 per type).
      Only used when focused -- the wide shot's knobs stay purely decorative
      (angle-only), same convention as AmpDevice/CabDevice. */
  knobs?: ParamHandle[];
}

/** Family-colored stompbox: decorative knobs in the wide shot, interactive when focused. */
export function PedalDevice({ pedal, bypassed, focused, onFocus, onToggleBypass, onJackRef, onSwap, knobs }: PedalDeviceProps) {
  // Matches --pw's 300px * 19% from mockups-v3.html's focused pedal knob --
  // only meaningful here since the wide shot renders `.pedal-deco-knob`
  // (sized purely by CSS) instead of a live <Knob>, never this constant.
  const FOCUSED_KNOB_SIZE = 57;

  const handleFootswitchClick = (e: MouseEvent) => {
    e.stopPropagation();
    onToggleBypass();
  };

  // Jacks live OUTSIDE `.pedal` (as siblings in `.pedal-shell`, painted AFTER
  // it so they sit on top) so they can protrude above the shell's top edge
  // without being clipped by `.pedal`'s own `overflow: hidden` (needed on
  // the shell to keep its rounded-corner shadow from rasterizing a seam —
  // see PedalDevice.css).
  const jacks = (
    <div className="pedal-jacks" aria-hidden="true">
      <span className="pedal-jack pedal-jack--in" ref={(el) => onJackRef?.("in", el)} />
      <span className="pedal-jack pedal-jack--out" ref={(el) => onJackRef?.("out", el)} />
    </div>
  );

  const body = (
    <div className="pedal-shell">
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
        <div className="pedal-panel">
          {pedal.knobs.map((k, i) =>
            focused ? (
              <div key={k.label} className="pedal-knob-well">
                <Knob
                  size={FOCUSED_KNOB_SIZE}
                  label={k.label}
                  value={knobs?.[i].value}
                  defaultValue={k.defaultValue}
                  format={k.format}
                  onChange={knobs?.[i].setValue}
                />
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
      {jacks}
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
