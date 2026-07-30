import "./TopBar.css";
import { ChainRail } from "./ChainRail";
import { Knob } from "./Knob";
import { PresetRow } from "./PresetRow";
import { VMeter } from "./VMeter";

export interface MeterReadout {
  peakDb: number;
  holdDb: number;
}

export interface TopBarProps {
  inputMeter: MeterReadout;
  outputMeter: MeterReadout;
}

/** Top utility bar: I/O knob+meter, chain rail + preset row, tuner. */
export function TopBar({ inputMeter, outputMeter }: TopBarProps) {
  return (
    <div className="topbar">
      <div className="topbar-side">
        <Knob size={34} label="Input" defaultValue={0.6} />
        <VMeter peakDb={inputMeter.peakDb} holdDb={inputMeter.holdDb} />
      </div>

      <div className="topbar-center">
        <ChainRail />
        <PresetRow />
      </div>

      <div className="topbar-side">
        <div className="topbar-tuner">
          <span className="topbar-label">Tuner</span>
          <span className="topbar-tuner-glyph">♪</span>
        </div>
        <VMeter peakDb={outputMeter.peakDb} holdDb={outputMeter.holdDb} />
        <Knob size={34} label="Output" defaultValue={0.6} />
      </div>
    </div>
  );
}
