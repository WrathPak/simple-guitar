import { useEffect, useRef, type ReactNode } from "react";
import "./Overlay.css";

export interface OverlayProps {
  title: string;
  onClose: () => void;
  children: ReactNode;
}

/**
 * The one overlay component (design rules §1/§5): a full-window layer over
 * the darkened, blurred room -- never an OS dialog or child window. Used by
 * the model picker, IR picker, and gate. Esc and a click on the scrim both
 * close it; the open/close transition rides the same camera timing tokens
 * as the room's focus dolly (150-220ms ease-out).
 */
export function Overlay({ title, onClose, children }: OverlayProps) {
  const panelRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    panelRef.current?.focus();

    function onKeyDown(e: KeyboardEvent) {
      if (e.key === "Escape") onClose();
    }
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [onClose]);

  return (
    <div className="overlay-wrap">
      <button type="button" className="overlay-scrim" aria-label="Close" onClick={onClose} />
      <div className="overlay-panel" role="dialog" aria-modal="true" aria-label={title} tabIndex={-1} ref={panelRef}>
        <div className="overlay-header">
          <div className="overlay-title">{title}</div>
          <button type="button" className="overlay-close" aria-label="Close" onClick={onClose}>
            ✕
          </button>
        </div>
        <div className="overlay-body">{children}</div>
      </div>
    </div>
  );
}
