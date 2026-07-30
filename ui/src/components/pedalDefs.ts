/**
 * Demo pedal data for the M0 floor row. Everything here is a display-only
 * stub: knob values and bypass state live in local component state, nothing
 * is wired to the audio engine yet (that's a later milestone).
 */

export type PedalFamily = "drive" | "delay" | "verb";

export interface PedalKnobDef {
  label: string;
  /** Rotation in degrees (-135..135) used to seed a matching normalized default. */
  angle: number;
}

export interface PedalDef {
  id: string;
  family: PedalFamily;
  name: string;
  knobs: [PedalKnobDef, PedalKnobDef, PedalKnobDef];
  /** Starting bypass state. */
  defaultBypassed: boolean;
}

export const DEMO_PEDALS: PedalDef[] = [
  {
    id: "screamer",
    family: "drive",
    name: "screamer",
    knobs: [
      { label: "Drive", angle: -20 },
      { label: "Tone", angle: 15 },
      { label: "Level", angle: 30 },
    ],
    defaultBypassed: true,
  },
  {
    id: "echoes",
    family: "delay",
    name: "echoes",
    knobs: [
      { label: "Time", angle: -35 },
      { label: "Fdbk", angle: 0 },
      { label: "Mix", angle: -50 },
    ],
    defaultBypassed: false,
  },
  {
    id: "chamber",
    family: "verb",
    name: "chamber",
    knobs: [
      { label: "Decay", angle: 40 },
      { label: "Tone", angle: 10 },
      { label: "Mix", angle: -45 },
    ],
    defaultBypassed: false,
  },
];
