import { useCallback, useEffect } from "react";
import "./PedalRow.css";
import type { JackPair } from "./CableLayer";
import { PedalDevice } from "./PedalDevice";
import type { PedalDef } from "./pedalDefs";
import { useChainReorder } from "./useChainReorder";

export interface PedalRowState extends PedalDef {
  bypassed: boolean;
}

export interface PedalRowProps {
  pedals: PedalRowState[];
  onFocusPedal: (id: string) => void;
  onToggleBypass: (id: string) => void;
  /** Commits a new left-to-right chain order after a drag or keyboard swap. */
  onReorder: (order: string[]) => void;
  /** Shared with the cable layer: id -> jack element registry. */
  jacksRef: React.MutableRefObject<Map<string, JackPair>>;
  /** Reported up so the cable layer knows to animate every frame. */
  onActivityChange?: (active: boolean) => void;
}

/** The floor: pedals in signal order, left to right, ending in a dim add spot. Drag or Shift+Arrow to reorder. */
export function PedalRow({ pedals, onFocusPedal, onToggleBypass, onReorder, jacksRef, onActivityChange }: PedalRowProps) {
  const order = pedals.map((p) => p.id);
  const reorder = useChainReorder(order, onReorder);

  useEffect(() => {
    onActivityChange?.(reorder.active);
  }, [reorder.active, onActivityChange]);

  const registerJack = useCallback(
    (id: string, which: "in" | "out", el: HTMLElement | null) => {
      const map = jacksRef.current;
      const existing = map.get(id) ?? { inEl: null, outEl: null };
      map.set(id, which === "in" ? { ...existing, inEl: el } : { ...existing, outEl: el });
    },
    [jacksRef],
  );

  return (
    <div className="pedal-row">
      {pedals.map((pedal) => {
        const offset = reorder.offsets[pedal.id] ?? 0;
        return (
          <div
            key={pedal.id}
            ref={(el) => reorder.registerSlot(pedal.id, el)}
            className={`pedal-slot${reorder.draggingId === pedal.id ? " pedal-slot--dragging" : ""}`}
            style={offset ? { transform: `translateX(${offset}px)` } : undefined}
            onPointerDown={(e) => reorder.onSlotPointerDown(e, pedal.id)}
            onPointerMove={reorder.onSlotPointerMove}
            onPointerUp={reorder.onSlotPointerUp}
            onPointerCancel={reorder.onSlotPointerUp}
            onClickCapture={(e) => {
              if (reorder.didDragRef.current) {
                e.preventDefault();
                e.stopPropagation();
              }
            }}
          >
            <PedalDevice
              pedal={pedal}
              bypassed={pedal.bypassed}
              focused={false}
              onFocus={() => onFocusPedal(pedal.id)}
              onToggleBypass={() => onToggleBypass(pedal.id)}
              onJackRef={(which, el) => registerJack(pedal.id, which, el)}
              onSwap={(direction) => reorder.onSwap(pedal.id, direction)}
            />
          </div>
        );
      })}
      <div className="pedal-row-add" aria-hidden="true">
        ＋
      </div>
    </div>
  );
}
