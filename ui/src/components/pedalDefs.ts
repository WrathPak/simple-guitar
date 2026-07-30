/**
 * Floor pedal data: id/family/name/knob layout for the three M2 pedals
 * (screamer/echoes/chamber), each wired to a real host param (see
 * schema/bridge.schema.json's ParamId enum / app/source/PluginProcessor.h).
 * `angle` is only used for the wide-shot's decorative knob rendering
 * (PedalDevice never live-binds knobs there, same convention as
 * AmpDevice/CabDevice); focused knobs bind live via each knob's `paramId`
 * (see Room.tsx, which owns the actual useParam calls and passes the
 * resulting ParamHandles down).
 */
import type { ParamId } from "../../../schema/gen/ts/bridge";
import { ECHOES_TIME_RANGE, format01AsTen, formatLogMs } from "./paramMath";

export type PedalFamily = "drive" | "delay" | "verb";

export interface PedalKnobDef {
  label: string;
  /** Rotation in degrees (-135..135) used for the wide-shot decorative cap only. */
  angle: number;
  paramId: ParamId;
  /** Seed value (matches the engine's own APVTS default) and double-click reset target. */
  defaultValue: number;
  /** Formats the normalized value for the knob's drag/hover tooltip. */
  format: (value: number) => string;
}

export interface PedalDef {
  id: string;
  family: PedalFamily;
  name: string;
  /** The pedal's footswitch/bypass param -- true means active (not bypassed). */
  onParamId: ParamId;
  /** Seed value for the on param (matches the engine's own APVTS default). */
  onDefault: number;
  knobs: [PedalKnobDef, PedalKnobDef, PedalKnobDef];
}

export const DEMO_PEDALS: PedalDef[] = [
  {
    id: "screamer",
    family: "drive",
    name: "screamer",
    onParamId: "screamerOn",
    onDefault: 0,
    knobs: [
      { label: "Drive", angle: -20, paramId: "screamerDrive", defaultValue: 0.5, format: format01AsTen },
      { label: "Tone", angle: 15, paramId: "screamerTone", defaultValue: 0.5, format: format01AsTen },
      { label: "Level", angle: 30, paramId: "screamerLevel", defaultValue: 0.7, format: format01AsTen },
    ],
  },
  {
    id: "echoes",
    family: "delay",
    name: "echoes",
    onParamId: "echoesOn",
    onDefault: 1,
    knobs: [
      {
        label: "Time",
        angle: -35,
        paramId: "echoesTime",
        defaultValue: 0.5,
        format: (v) => formatLogMs(v, ECHOES_TIME_RANGE),
      },
      { label: "Fdbk", angle: 0, paramId: "echoesFeedback", defaultValue: 0.4, format: format01AsTen },
      { label: "Mix", angle: -50, paramId: "echoesMix", defaultValue: 0.25, format: format01AsTen },
    ],
  },
  {
    id: "chamber",
    family: "verb",
    name: "chamber",
    onParamId: "chamberOn",
    onDefault: 1,
    knobs: [
      { label: "Decay", angle: 40, paramId: "chamberDecay", defaultValue: 0.5, format: format01AsTen },
      { label: "Tone", angle: 10, paramId: "chamberTone", defaultValue: 0.5, format: format01AsTen },
      { label: "Mix", angle: -45, paramId: "chamberMix", defaultValue: 0.22, format: format01AsTen },
    ],
  },
];

/** DEMO_PEDALS indexed by id, for O(1) lookup from a chainOrder id list. */
export const PEDAL_DEFS_BY_ID: Record<string, PedalDef> = Object.fromEntries(DEMO_PEDALS.map((p) => [p.id, p]));
