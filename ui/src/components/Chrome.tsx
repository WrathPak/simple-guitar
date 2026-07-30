import { useState } from "react";
import "./Chrome.css";
import { EdgeMeter } from "./EdgeMeter";

export interface MeterReadout {
  peakDb: number;
  holdDb: number;
}

export interface ChromeProps {
  /** Current preset's display name, or null -- rendered as dimmed "no preset". */
  presetName: string | null;
  /** Amber dot -- only ever shown while dirty. */
  dirty: boolean;
  /** Disables ‹ › -- there's nothing to step to when the Presets library is empty. */
  hasPresets: boolean;
  onPrevPreset: () => void;
  onNextPreset: () => void;
  /** Click the pill name -- opens the presets browser overlay. */
  onOpenPresets: () => void;
  /** The pill's small "save" action: saves the current preset, or opens save-as if there is none. */
  onSave: () => void;
  /** ⋯ menu "Save as" -- always opens the save-as overlay, current preset or not. */
  onOpenSaveAs: () => void;
  inputMeter: MeterReadout;
  outputMeter: MeterReadout;
  hint: string;
  /** Contextual line above the hint, shown only while a relevant device is focused. */
  contextual?: string;
  /** Opens the gate overlay -- gate is rack processing behind this menu, not a pedal on the floor. */
  onSelectGate?: () => void;
  /** Opens the plugins overlay (what's installed + the scan action). */
  onSelectPlugins?: () => void;
}

// Library/Settings/Tuner are display-only placeholders for now (no menu action wired yet).
const STATIC_MENU_ITEMS = ["Library", "Settings", "Tuner"];

/** The three chrome whispers: wordmark, preset pill + menu, edge meters, hint line. */
export function Chrome({
  presetName,
  dirty,
  hasPresets,
  onPrevPreset,
  onNextPreset,
  onOpenPresets,
  onSave,
  onOpenSaveAs,
  inputMeter,
  outputMeter,
  hint,
  contextual,
  onSelectGate,
  onSelectPlugins,
}: ChromeProps) {
  const [menuOpen, setMenuOpen] = useState(false);

  return (
    <>
      <div className="chrome-wordmark">Simple Guitar</div>

      <div className="chrome-presetpill">
        <div className="chrome-pill">
          <button type="button" className="chrome-pill-nav" aria-label="Previous preset" disabled={!hasPresets} onClick={onPrevPreset}>
            ‹
          </button>
          <button type="button" className="chrome-pill-name-btn" onClick={onOpenPresets}>
            <span className={`chrome-pill-name${presetName === null ? " chrome-pill-name--empty" : ""}`}>
              {presetName ?? "no preset"}
            </span>
            {dirty && <span className="chrome-pill-dot" aria-hidden="true" />}
          </button>
          <button type="button" className="chrome-pill-nav" aria-label="Next preset" disabled={!hasPresets} onClick={onNextPreset}>
            ›
          </button>
          <button type="button" className="chrome-pill-save" aria-label="Save preset" onClick={onSave}>
            save
          </button>
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
              <button
                type="button"
                role="menuitem"
                className="chrome-menu-item"
                onClick={() => {
                  setMenuOpen(false);
                  onOpenPresets();
                }}
              >
                Presets
              </button>
              <button
                type="button"
                role="menuitem"
                className="chrome-menu-item"
                onClick={() => {
                  setMenuOpen(false);
                  onOpenSaveAs();
                }}
              >
                Save as
              </button>
              {STATIC_MENU_ITEMS.map((item) => (
                <button key={item} type="button" role="menuitem" className="chrome-menu-item" disabled>
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
              <button
                type="button"
                role="menuitem"
                className="chrome-menu-item"
                onClick={() => {
                  setMenuOpen(false);
                  onSelectPlugins?.();
                }}
              >
                Plugins
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
