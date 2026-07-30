import { useEffect, useRef, type ReactNode } from "react";
import "./Overlay.css";

export interface OverlayProps {
  title: string;
  onClose: () => void;
  children: ReactNode;
}

/** Focusable-per-tab-order elements a keyboard user could land on inside a panel. */
const FOCUSABLE_SELECTOR = 'button:not(:disabled), [href], input:not(:disabled), select:not(:disabled), textarea:not(:disabled), [tabindex]:not([tabindex="-1"])';

/**
 * The one overlay component (design rules §1/§5): a full-window layer over
 * the darkened, blurred room -- never an OS dialog or child window. Used by
 * the model picker, IR picker, and gate. Esc and a click on the scrim both
 * close it; the open/close transition rides the same camera timing tokens
 * as the room's focus dolly (150-220ms ease-out).
 *
 * Keyboard behavior lives here once, for every overlay: Tab/Shift+Tab is
 * trapped inside the panel (aria-modal="true" is a lie otherwise), and the
 * element that had focus before the overlay opened -- almost always the
 * button that triggered it -- gets focus back once it closes, so tabbing
 * never falls into the void.
 */
export function Overlay({ title, onClose, children }: OverlayProps) {
  const panelRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const previouslyFocused = document.activeElement as HTMLElement | null;

    // Don't steal focus from a descendant that already grabbed it on mount
    // (e.g. SaveAsOverlay's autofocused name input, whose own mount effect
    // runs before this one -- child effects fire before parent effects).
    if (!panelRef.current?.contains(document.activeElement)) {
      panelRef.current?.focus();
    }

    function onKeyDown(e: KeyboardEvent) {
      if (e.key === "Escape") {
        onClose();
        return;
      }
      if (e.key !== "Tab") return;
      const panel = panelRef.current;
      if (!panel) return;
      const focusable = Array.from(panel.querySelectorAll<HTMLElement>(FOCUSABLE_SELECTOR));
      if (focusable.length === 0) {
        e.preventDefault();
        return;
      }
      const first = focusable[0];
      const last = focusable[focusable.length - 1];
      if (e.shiftKey && document.activeElement === first) {
        e.preventDefault();
        last.focus();
      } else if (!e.shiftKey && document.activeElement === last) {
        e.preventDefault();
        first.focus();
      }
    }
    window.addEventListener("keydown", onKeyDown);
    return () => {
      window.removeEventListener("keydown", onKeyDown);
      // Return focus to whatever opened this overlay (usually already gone
      // if the trigger was itself a menu item that unmounted, in which case
      // this is a harmless no-op).
      previouslyFocused?.focus?.();
    };
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
