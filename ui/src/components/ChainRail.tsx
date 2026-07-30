import type { ComponentType } from "react";
import "./ChainRail.css";
import { AmpIcon, CabIcon, CompIcon, DelayIcon, DriveIcon, GateIcon, ReverbIcon, type IconProps } from "./icons";

type RailState = "on" | "off" | "sel";

interface RailSlot {
  key: string;
  title: string;
  state: RailState;
  Icon: ComponentType<IconProps>;
}

// M0 placeholder: static states, not wired to a real chain yet. Mirrors
// design/mockups-v2.html Direction D "View 1" exactly (the reference render).
const SLOTS: RailSlot[] = [
  { key: "gate", title: "Gate", state: "on", Icon: GateIcon },
  { key: "comp", title: "Comp", state: "on", Icon: CompIcon },
  { key: "drive", title: "Drive", state: "off", Icon: DriveIcon },
  { key: "amp", title: "Amp", state: "sel", Icon: AmpIcon },
  { key: "cab", title: "Cab", state: "on", Icon: CabIcon },
  { key: "delay", title: "Delay", state: "on", Icon: DelayIcon },
  { key: "reverb", title: "Reverb", state: "on", Icon: ReverbIcon },
];

/**
 * The chain rail: the entire signal-chain UI (DESIGN.md §3). Static
 * placeholder for M0 — no drag-to-reorder or staging wired yet.
 */
export function ChainRail() {
  return (
    <div className="rail">
      {SLOTS.map(({ key, title, state, Icon }) => (
        <span key={key} className={`rail-icon rail-icon--${state}`} title={title}>
          <Icon />
        </span>
      ))}
      <span className="rail-plus" title="Add">
        ＋
      </span>
    </div>
  );
}
