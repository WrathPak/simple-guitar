import { useEffect, useState } from "react";
import type { LibraryEntry, LoadResult } from "../bridge";
import "./LibraryOverlay.css";
import { Overlay } from "./Overlay";

export interface LibraryOverlayProps {
  /** Which loader this picker drives -- also which loadResult.kind it reacts to. */
  kind: "nam" | "ir";
  title: string;
  /** Shown instead of the list when the managed library folder has nothing in it. */
  emptyCopy: string;
  library: LibraryEntry[];
  /** Most recent loadResult from the bridge, of any kind -- filtered internally by `kind`. */
  loadResult: LoadResult | null;
  onSelect: (path: string) => void;
  onClose: () => void;
  /** Called once on mount, e.g. to requestRigState so the list is fresh. */
  onOpen?: () => void;
}

/** Model/IR picker: a quiet list of library entries by name. Click one to load it. */
export function LibraryOverlay({ kind, title, emptyCopy, library, loadResult, onSelect, onClose, onOpen }: LibraryOverlayProps) {
  const [pendingPath, setPendingPath] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    onOpen?.();
    // Runs once, on mount only -- the overlay is remounted fresh each time it opens.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    if (pendingPath === null || loadResult === null || loadResult.kind !== kind) return;
    if (loadResult.ok) {
      onClose();
    } else {
      setPendingPath(null);
      setError(loadResult.message);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [loadResult]);

  const handleSelect = (entry: LibraryEntry) => {
    setError(null);
    setPendingPath(entry.path);
    onSelect(entry.path);
  };

  return (
    <Overlay title={title} onClose={onClose}>
      {library.length === 0 ? (
        <div className="library-overlay-empty">{emptyCopy}</div>
      ) : (
        <div className="library-overlay-list">
          {library.map((entry) => (
            <button
              key={entry.path}
              type="button"
              className="library-overlay-row"
              disabled={pendingPath !== null}
              onClick={() => handleSelect(entry)}
            >
              <span className="library-overlay-row-name">{entry.name}</span>
              {pendingPath === entry.path && <span className="library-overlay-row-status">loading</span>}
            </button>
          ))}
        </div>
      )}
      {error && <div className="library-overlay-error">{error}</div>}
    </Overlay>
  );
}
