import { useCallback, useEffect, useRef } from "react";
import "./PedalRow.css";
import type { JackPair } from "./CableLayer";
import { PedalDevice } from "./PedalDevice";
import { SLOT_COUNT, slotDomId, type PedalTypeDef } from "./pedalDefs";
import { useChainReorder } from "./useChainReorder";

/** One occupied floor slot, in visual (= signal) order. Empty slots collapse and never reach the row. */
export interface FloorPedal {
  /** Which of the six slots this pedal lives in (0..5). Slot indices can be sparse; the row renders them contiguously. */
  slot: number;
  def: PedalTypeDef;
  bypassed: boolean;
}

export interface PedalRowProps {
  pedals: FloorPedal[];
  onFocusPedal: (slot: number) => void;
  onToggleBypass: (slot: number) => void;
  /** Commits a reorder after a drag or keyboard swap, in slot indices (the movePedal message shape). */
  onMovePedal: (from: number, to: number) => void;
  /** The dashed ＋ spot after the last pedal (hidden when all six slots are full). */
  onAddPedal: () => void;
  /** Shared with the cable layer: id -> jack element registry. */
  jacksRef: React.MutableRefObject<Map<string, JackPair>>;
  /** Reported up so the cable layer knows to animate every frame. */
  onActivityChange?: (active: boolean) => void;
}

/** The floor: occupied slots in signal order, left to right, ending in a dim add spot. Drag or Shift+Arrow to reorder. */
export function PedalRow({ pedals, onFocusPedal, onToggleBypass, onMovePedal, onAddPedal, jacksRef, onActivityChange }: PedalRowProps) {
  const order = pedals.map((p) => slotDomId(p.slot));

  const commitMove = useCallback(
    (fromIndex: number, toIndex: number) => {
      const from = pedals[fromIndex]?.slot;
      const to = pedals[toIndex]?.slot;
      if (from === undefined || to === undefined) return;
      onMovePedal(from, to);
    },
    [pedals, onMovePedal],
  );

  const reorder = useChainReorder(order, commitMove);

  useEffect(() => {
    onActivityChange?.(reorder.active);
  }, [reorder.active, onActivityChange]);

  // A pedal that appears after the initial render (palette pick, preset
  // load adding gear) settles into place with a one-shot enter animation.
  // The class sticks once earned -- a CSS animation only plays on class
  // *addition*, so keeping it is what protects the animation from being
  // cancelled by an unrelated re-render mid-flight.
  const knownIdsRef = useRef<Set<string> | null>(null);
  const enteringIdsRef = useRef(new Set<string>());
  if (knownIdsRef.current === null) {
    knownIdsRef.current = new Set(order);
  } else {
    const known = knownIdsRef.current;
    const entering = enteringIdsRef.current;
    for (const id of known) {
      if (!order.includes(id)) {
        known.delete(id);
        entering.delete(id); // freed up: re-adding this slot later animates again.
      }
    }
    for (const id of order) {
      if (!known.has(id)) {
        known.add(id);
        entering.add(id);
      }
    }
  }

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
        const id = slotDomId(pedal.slot);
        const offset = reorder.offsets[id] ?? 0;
        const classes = [
          "pedal-slot",
          reorder.draggingId === id ? "pedal-slot--dragging" : "",
          enteringIdsRef.current.has(id) ? "pedal-slot--enter" : "",
        ]
          .filter(Boolean)
          .join(" ");
        return (
          <div
            key={id}
            ref={(el) => reorder.registerSlot(id, el)}
            className={classes}
            style={offset ? { transform: `translateX(${offset}px)` } : undefined}
            onPointerDown={(e) => reorder.onSlotPointerDown(e, id)}
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
              pedal={pedal.def}
              bypassed={pedal.bypassed}
              focused={false}
              onFocus={() => onFocusPedal(pedal.slot)}
              onToggleBypass={() => onToggleBypass(pedal.slot)}
              onJackRef={(which, el) => registerJack(id, which, el)}
              onSwap={(direction) => reorder.onSwap(id, direction)}
            />
          </div>
        );
      })}
      {pedals.length < SLOT_COUNT && (
        <button type="button" className="pedal-row-add" aria-label="add pedal" onClick={onAddPedal}>
          ＋
        </button>
      )}
    </div>
  );
}
