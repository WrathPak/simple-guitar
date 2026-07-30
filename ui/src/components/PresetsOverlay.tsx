import { useEffect, useState } from "react";
import type { LibraryEntry, PresetResult } from "../bridge";
import "./LibraryOverlay.css";
import { Overlay } from "./Overlay";

export interface PresetsOverlayProps {
  presets: LibraryEntry[];
  /** The current preset's path, if any -- used only to mark its row quietly. */
  currentPath: string | null;
  /** Most recent presetResult from the bridge. */
  presetResult: PresetResult | null;
  onSelect: (path: string) => void;
  onClose: () => void;
  /** Called once on mount, e.g. to requestPresets so the list is fresh. */
  onOpen?: () => void;
}

const EMPTY_COPY = "no presets yet — save one";

/** Preset browser: a quiet list of saved presets by name. Click one to load it; the current preset's row is marked quietly (no badge, just a low-contrast label). Same shape as LibraryOverlay's model/IR pickers. */
export function PresetsOverlay({ presets, currentPath, presetResult, onSelect, onClose, onOpen }: PresetsOverlayProps) {
  const [pendingPath, setPendingPath] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    onOpen?.();
    // Runs once, on mount only -- the overlay is remounted fresh each time it opens.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    if (pendingPath === null || presetResult === null) return;
    if (presetResult.ok) {
      onClose();
    } else {
      setPendingPath(null);
      setError(presetResult.message);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [presetResult]);

  const handleSelect = (entry: LibraryEntry) => {
    setError(null);
    setPendingPath(entry.path);
    onSelect(entry.path);
  };

  return (
    <Overlay title="Presets" onClose={onClose}>
      {presets.length === 0 ? (
        <div className="library-overlay-empty">{EMPTY_COPY}</div>
      ) : (
        <div className="library-overlay-list">
          {presets.map((entry) => (
            <button
              key={entry.path}
              type="button"
              className="library-overlay-row"
              disabled={pendingPath !== null}
              onClick={() => handleSelect(entry)}
            >
              <span className="library-overlay-row-name">{entry.name}</span>
              {pendingPath === entry.path ? (
                <span className="library-overlay-row-status">loading</span>
              ) : entry.path === currentPath ? (
                <span className="library-overlay-row-status">current</span>
              ) : null}
            </button>
          ))}
        </div>
      )}
      {error && <div className="library-overlay-error">{error}</div>}
    </Overlay>
  );
}
