import { useEffect, useRef, useState } from "react";
import "./App.css";
import { getSliderState, subscribeMeterFrame, type MeterFrame } from "./bridge";
import { BotBar } from "./components/BotBar";
import { Knob } from "./components/Knob";
import { METER_MIN_DB, nextPeakHold } from "./components/meterMath";
import { Stage } from "./components/Stage";
import { TopBar } from "./components/TopBar";

const OUTPUT_SLIDER_ID = "outputGain";
const OUTPUT_DEFAULT_VALUE = 0.75;

function formatGainDb(value: number): string {
  // Cosmetic mapping for the M0 demo knob's tooltip: 0..1 -> -60..+6 dB.
  const db = METER_MIN_DB + value * (6 - METER_MIN_DB);
  return `${db >= 0 ? "+" : ""}${db.toFixed(1)} dB`;
}

export default function App() {
  const outputSlider = useRef(getSliderState(OUTPUT_SLIDER_ID, OUTPUT_DEFAULT_VALUE));
  const [outputValue, setOutputValue] = useState(outputSlider.current.value);

  const [inMeter, setInMeter] = useState({ peakDb: METER_MIN_DB, holdDb: METER_MIN_DB });
  const [outMeter, setOutMeter] = useState({ peakDb: METER_MIN_DB, holdDb: METER_MIN_DB });

  useEffect(() => {
    return outputSlider.current.subscribe(setOutputValue);
  }, []);

  useEffect(() => {
    const unsubscribe = subscribeMeterFrame((frame: MeterFrame) => {
      setInMeter((prev) => ({
        peakDb: frame.inPeakDb,
        holdDb: nextPeakHold(prev.holdDb, frame.inPeakDb),
      }));
      setOutMeter((prev) => ({
        peakDb: frame.outPeakDb,
        holdDb: nextPeakHold(prev.holdDb, frame.outPeakDb),
      }));
    });
    return unsubscribe;
  }, []);

  return (
    <div className="app">
      <TopBar inputMeter={inMeter} outputMeter={outMeter} />
      <Stage>
        <Knob
          size={74}
          label="Output"
          value={outputValue}
          defaultValue={OUTPUT_DEFAULT_VALUE}
          format={formatGainDb}
          onChange={(next) => outputSlider.current.setValue(next)}
        />
      </Stage>
      <BotBar />
    </div>
  );
}
