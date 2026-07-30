import { useState } from "react";
import "./Chrome.css";
import { EdgeMeter } from "./EdgeMeter";

export interface MeterReadout {
  peakDb: number;
  holdDb: number;
}

export interface ChromeProps {
  presetName: string;
  unsaved: boolean;
  inputMeter: MeterReadout;
  outputMeter: MeterReadout;
  hint: string;
  /** Contextual line above the hint, shown only while a relevant device is focused. */
  contextual?: string;
  /** Opens the gate overlay -- gate is rack processing behind this menu, not a pedal on the floor. */
  onSelectGate?: () => void;
}

// Presets/Library/Settings/Tuner are display-only placeholders for now (no menu action wired yet).
const STATIC_MENU_ITEMS = ["Presets", "Library", "Settings", "Tuner"];

/** The three chrome whispers: wordmark, preset pill + menu, edge meters, hint line. */
export function Chrome({ presetName, unsaved, inputMeter, outputMeter, hint, contextual, onSelectGate }: ChromeProps) {
  const [menuOpen, setMenuOpen] = useState(false);

  return (
    <>
      <div className="chrome-wordmark">Simple Guitar</div>

      <div className="chrome-presetpill">
        <div className="chrome-pill">
          <span className="chrome-pill-name">{presetName}</span>
          {unsaved && <span className="chrome-pill-dot" aria-hidden="true" />}
        </div>
        <button
          type="button"
          className="chrome-menu-btn"
          aria-haspopup="menu"
          aria-expanded={menuOpen}
          onClick={() => setMenuOpen((open) => !open)}
        >
          ⋯
        </button>

        {menuOpen && (
          <>
            <button type="button" className="chrome-menu-scrim" aria-label="Close menu" onClick={() => setMenuOpen(false)} />
            <div className="chrome-menu" role="menu">
              {STATIC_MENU_ITEMS.map((item) => (
                <button key={item} type="button" role="menuitem" className="chrome-menu-item">
                  {item}
                </button>
              ))}
              <button
                type="button"
                role="menuitem"
                className="chrome-menu-item"
                onClick={() => {
                  setMenuOpen(false);
                  onSelectGate?.();
                }}
              >
                Gate
              </button>
            </div>
          </>
        )}
      </div>

      <EdgeMeter side="l" label="input level" peakDb={inputMeter.peakDb} holdDb={inputMeter.holdDb} />
      <EdgeMeter side="r" label="output level" peakDb={outputMeter.peakDb} holdDb={outputMeter.holdDb} />

      <div className="chrome-hint-stack">
        {contextual && <div className="chrome-contextual">{contextual}</div>}
        <div className="chrome-hint">{hint}</div>
      </div>
    </>
  );
}
