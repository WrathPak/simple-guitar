/**
 * Pedal type registry for the 6-slot floor (schema v5). Each of the six
 * built-in pedal types (screamer/echoes/chamber/squeeze/wobble/shiver) is
 * described once: display name, family (drives the shell/LED color CSS
 * class, see PedalDevice.css + tokens.css), and knob layout. A knob binds to
 * one of the slot's four generic P params (slot{N}P1..P4) via `param`; which
 * slot's params it actually reads is decided by whoever renders it (Room
 * owns the useParam calls and passes live handles down).
 *
 * A seventh type -- a hosted third-party plugin -- has no fixed entry here:
 * its name comes from the engine per slot, so `pluginPedalDef` builds a def
 * on demand instead (same shape, so every consumer stays type-agnostic).
 *
 * `angle` is only used for the wide-shot's decorative knob rendering
 * (PedalDevice never live-binds knobs there, same convention as
 * AmpDevice/CabDevice).
 */
import { PLUGIN_PEDAL_TYPE } from "../bridge";
import type { ParamId } from "../../../schema/gen/ts/bridge";
import { ECHOES_TIME_RANGE, format01AsTen, formatLogMs } from "./paramMath";

/** How many pedal slots the floor has. */
export const SLOT_COUNT = 6;

/** slot{N}Type value meaning "nothing in this slot". */
export const EMPTY_SLOT_TYPE = 0;

/** Shell/LED family, mapped to CSS via `pedal--${family}` (family gradients/LEDs live in tokens.css + PedalDevice.css). */
export type PedalFamily = "drive" | "dynamics" | "mod" | "delay" | "verb" | "utility";

export interface PedalKnobDef {
  label: string;
  /** Rotation in degrees (-135..135) used for the wide-shot decorative cap only. */
  angle: number;
  /** Which of the slot's generic P params this knob binds to (1..4 -> slot{N}P1..P4). */
  param: 1 | 2 | 3 | 4;
  /** Double-click reset target (normalized; the generic slot params default to 0.5 engine-side). */
  defaultValue: number;
  /** Formats the normalized value for the knob's drag/hover tooltip. */
  format: (value: number) => string;
}

export interface PedalTypeDef {
  /** The slot{N}Type choice value (1..6). 0 is the empty slot, which has no def. */
  type: number;
  /** Stable lowercase id, doubling as the display name (terse, plain). */
  id: string;
  name: string;
  family: PedalFamily;
  knobs: PedalKnobDef[];
}

/** The six placeable pedal types, in palette order (slot{N}Type 1..6). */
export const PEDAL_TYPES: PedalTypeDef[] = [
  {
    type: 1,
    id: "screamer",
    name: "screamer",
    family: "drive",
    knobs: [
      { label: "Drive", angle: -20, param: 1, defaultValue: 0.5, format: format01AsTen },
      { label: "Tone", angle: 15, param: 2, defaultValue: 0.5, format: format01AsTen },
      { label: "Level", angle: 30, param: 3, defaultValue: 0.5, format: format01AsTen },
    ],
  },
  {
    type: 2,
    id: "echoes",
    name: "echoes",
    family: "delay",
    knobs: [
      { label: "Time", angle: -35, param: 1, defaultValue: 0.5, format: (v) => formatLogMs(v, ECHOES_TIME_RANGE) },
      { label: "Fdbk", angle: 0, param: 2, defaultValue: 0.5, format: format01AsTen },
      { label: "Mix", angle: -50, param: 3, defaultValue: 0.5, format: format01AsTen },
    ],
  },
  {
    type: 3,
    id: "chamber",
    name: "chamber",
    family: "verb",
    knobs: [
      { label: "Decay", angle: 40, param: 1, defaultValue: 0.5, format: format01AsTen },
      { label: "Tone", angle: 10, param: 2, defaultValue: 0.5, format: format01AsTen },
      { label: "Mix", angle: -45, param: 3, defaultValue: 0.5, format: format01AsTen },
    ],
  },
  {
    type: 4,
    id: "squeeze",
    name: "squeeze",
    family: "dynamics",
    knobs: [
      { label: "Amount", angle: -10, param: 1, defaultValue: 0.5, format: format01AsTen },
      { label: "Attack", angle: -30, param: 2, defaultValue: 0.5, format: format01AsTen },
      { label: "Release", angle: 20, param: 3, defaultValue: 0.5, format: format01AsTen },
      { label: "Level", angle: 35, param: 4, defaultValue: 0.5, format: format01AsTen },
    ],
  },
  {
    type: 5,
    id: "wobble",
    name: "wobble",
    family: "mod",
    knobs: [
      { label: "Rate", angle: -25, param: 1, defaultValue: 0.5, format: format01AsTen },
      { label: "Depth", angle: 10, param: 2, defaultValue: 0.5, format: format01AsTen },
      { label: "Mix", angle: -40, param: 3, defaultValue: 0.5, format: format01AsTen },
    ],
  },
  {
    type: 6,
    id: "shiver",
    name: "shiver",
    family: "mod",
    knobs: [
      { label: "Rate", angle: -15, param: 1, defaultValue: 0.5, format: format01AsTen },
      { label: "Depth", angle: 25, param: 2, defaultValue: 0.5, format: format01AsTen },
      { label: "Shape", angle: 0, param: 3, defaultValue: 0.5, format: format01AsTen },
    ],
  },
];

/**
 * The one knob a hosted plugin gets: the engine-side wet/dry mix on P1.
 * Everything else the plugin exposes lives in its own editor.
 */
const PLUGIN_MIX_KNOB: PedalKnobDef = { label: "Mix", angle: 0, param: 1, defaultValue: 0.5, format: format01AsTen };

/** Stand-in name for a type-7 slot with no plugin picked yet -- the blink between setSlotType and setSlotPlugin resolving. */
const UNNAMED_PLUGIN = "plugin";

/**
 * Builds the def for a hosted-plugin slot. The plugin's own name is the
 * pedal's name (the engine sends it on rigState.slots[n].pluginName), the
 * shell is the matte-black utility family, and the panel carries the single
 * mix knob.
 */
export function pluginPedalDef(pluginName: string | null | undefined): PedalTypeDef {
  return {
    type: PLUGIN_PEDAL_TYPE,
    id: "plugin",
    name: pluginName ?? UNNAMED_PLUGIN,
    family: "utility",
    knobs: [PLUGIN_MIX_KNOB],
  };
}

/** Registry lookup by slot{N}Type value. Returns undefined for 0 (empty), 7 (use pluginPedalDef) or anything out of range. */
export function pedalTypeDef(type: number): PedalTypeDef | undefined {
  return PEDAL_TYPES.find((def) => def.type === type);
}

/** The relay/param id for one of a slot's params, e.g. slotParamId(2, "P1") -> "slot2P1". */
export function slotParamId(slot: number, part: "Type" | "On" | "P1" | "P2" | "P3" | "P4"): ParamId {
  return `slot${slot}${part}` as ParamId;
}

/** A slot's stable DOM/registry id, shared by the pedal row and CableLayer's order list. */
export function slotDomId(slot: number): string {
  return `slot${slot}`;
}
