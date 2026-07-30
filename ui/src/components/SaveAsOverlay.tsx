import { useEffect, useRef, useState } from "react";
import type { PresetResult } from "../bridge";
import "./SaveAsOverlay.css";
import { Overlay } from "./Overlay";

export interface SaveAsOverlayProps {
  /** Seed value for the input -- the current preset's name, if any, so "save as" defaults to a rename/duplicate of what's open. */
  initialName?: string;
  /** Most recent presetResult from the bridge. */
  presetResult: PresetResult | null;
  onSave: (name: string) => void;
  onClose: () => void;
}

/**
 * The app's first text field: a single quiet, monochrome, autofocused name
 * input. Enter saves (if the trimmed name isn't empty), Esc cancels (Overlay
 * already closes on Escape via its window-level listener). Shows the
 * engine's failure message inline if the save doesn't succeed.
 */
export function SaveAsOverlay({ initialName = "", presetResult, onSave, onClose }: SaveAsOverlayProps) {
  const [name, setName] = useState(initialName);
  const [pending, setPending] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const inputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    inputRef.current?.focus();
    inputRef.current?.select();
  }, []);

  useEffect(() => {
    if (!pending || presetResult === null) return;
    if (presetResult.ok) {
      onClose();
    } else {
      setPending(false);
      setError(presetResult.message);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [presetResult]);

  const trySave = () => {
    const trimmed = name.trim();
    if (trimmed === "" || pending) return;
    setError(null);
    setPending(true);
    onSave(trimmed);
  };

  return (
    <Overlay title="Save as" onClose={onClose}>
      <div className="saveas-overlay-body">
        <input
          ref={inputRef}
          type="text"
          className="saveas-overlay-input"
          placeholder="preset name"
          aria-label="Preset name"
          value={name}
          disabled={pending}
          onChange={(e) => setName(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === "Enter") {
              e.preventDefault();
              trySave();
            }
          }}
        />
        {error && <div className="saveas-overlay-error">{error}</div>}
      </div>
    </Overlay>
  );
}
