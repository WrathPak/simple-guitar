import { useEffect, useRef, useState } from "react";
import { getSliderState, subscribeMeterFrame, type MeterFrame } from "../bridge";
import { AmpDevice } from "./AmpDevice";
import { Backline } from "./Backline";
import { Chrome } from "./Chrome";
import { METER_MIN_DB, nextPeakHold } from "./meterMath";
import { DEMO_PEDALS } from "./pedalDefs";
import { PedalDevice } from "./PedalDevice";
import { PedalRow, type PedalRowState } from "./PedalRow";
import "./Room.css";

const OUTPUT_SLIDER_ID = "outputGain";
const OUTPUT_DEFAULT_VALUE = 0.75;

const WIDE_HINT = "Click gear to focus · drag pedals to reorder · footswitch = bypass";
const FOCUSED_HINT = "Click outside or press Esc to step back";
const DELAY_CONTEXTUAL = "Tap · 122 BPM · Sync ¼";

/** Camera target: null = wide shot, "amp" = amp focused, else a pedal id. */
type FocusTarget = "amp" | string | null;

/** The whole cinematic scene: one room, the rig in it, and the chrome around it. */
export function Room() {
  const outputSlider = useRef(getSliderState(OUTPUT_SLIDER_ID, OUTPUT_DEFAULT_VALUE));
  const [outputValue, setOutputValue] = useState(outputSlider.current.value);

  const [inMeter, setInMeter] = useState({ peakDb: METER_MIN_DB, holdDb: METER_MIN_DB });
  const [outMeter, setOutMeter] = useState({ peakDb: METER_MIN_DB, holdDb: METER_MIN_DB });

  const [pedals, setPedals] = useState<PedalRowState[]>(() =>
    DEMO_PEDALS.map((pedal) => ({ ...pedal, bypassed: pedal.defaultBypassed })),
  );
  const [focus, setFocus] = useState<FocusTarget>(null);

  useEffect(() => outputSlider.current.subscribe(setOutputValue), []);

  useEffect(() => {
    return subscribeMeterFrame((frame: MeterFrame) => {
      setInMeter((prev) => ({ peakDb: frame.inPeakDb, holdDb: nextPeakHold(prev.holdDb, frame.inPeakDb) }));
      setOutMeter((prev) => ({ peakDb: frame.outPeakDb, holdDb: nextPeakHold(prev.holdDb, frame.outPeakDb) }));
    });
  }, []);

  useEffect(() => {
    if (focus === null) return;
    function onKeyDown(e: KeyboardEvent) {
      if (e.key === "Escape") setFocus(null);
    }
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [focus]);

  const toggleBypass = (id: string) => {
    setPedals((prev) => prev.map((p) => (p.id === id ? { ...p, bypassed: !p.bypassed } : p)));
  };

  const focusedPedal = typeof focus === "string" && focus !== "amp" ? pedals.find((p) => p.id === focus) : undefined;
  const hint = focus === null ? WIDE_HINT : FOCUSED_HINT;
  const contextual = focusedPedal?.family === "delay" ? DELAY_CONTEXTUAL : undefined;

  return (
    <div className="room">
      <div className="room-scene-wrap" onClick={focus !== null ? () => setFocus(null) : undefined}>
        <div className={`room-scene${focus !== null ? " room-scene--blurred" : ""}`}>
          <div className="room-floor" />
          <Backline onFocusAmp={() => setFocus("amp")} />
          <svg className="room-cable" viewBox="0 0 560 26" aria-hidden="true">
            <path d="M10 4 C 120 30, 440 30, 550 4" fill="none" stroke="#222226" strokeWidth="2" />
          </svg>
          <PedalRow pedals={pedals} onFocusPedal={setFocus} onToggleBypass={toggleBypass} />
        </div>
      </div>

      <div className="room-focus-wrap">
        {focus === "amp" && (
          <AmpDevice
            focused
            outputValue={outputValue}
            onOutputChange={(next) => outputSlider.current.setValue(next)}
          />
        )}
        {focusedPedal && (
          <PedalDevice
            pedal={focusedPedal}
            bypassed={focusedPedal.bypassed}
            focused
            onToggleBypass={() => toggleBypass(focusedPedal.id)}
          />
        )}
      </div>

      <Chrome
        presetName="Blues Breakup 02"
        unsaved
        inputMeter={inMeter}
        outputMeter={outMeter}
        hint={hint}
        contextual={contextual}
      />
    </div>
  );
}
