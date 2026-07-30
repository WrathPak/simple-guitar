import { useCallback, useEffect, useRef, useState } from "react";
import type { PointerEvent as ReactPointerEvent } from "react";
import { type ChainOrder, dragOverIndex, exceedsDragThreshold } from "./chainOrder";
import { isSpringSettled, PEDAL_SPRING, stepSpring, type SpringState } from "./spring";

const FALLBACK_SLOT_STEP = 204; // pedal width (170) + row gap (34), used before anything has been measured

function freshSpring(): SpringState {
  return { x: 0, v: 0 };
}

export interface ChainReorderApi {
  /** Per-pedal-id live x offset (px) — apply as `translateX` on each pedal's slot. */
  offsets: Record<string, number>;
  draggingId: string | null;
  /** True while a drag or a settle-spring is running — drives the cable layer's animated recompute. */
  active: boolean;
  registerSlot: (id: string, el: HTMLElement | null) => void;
  onSlotPointerDown: (e: ReactPointerEvent<HTMLElement>, id: string) => void;
  onSlotPointerMove: (e: ReactPointerEvent<HTMLElement>) => void;
  onSlotPointerUp: (e: ReactPointerEvent<HTMLElement>) => void;
  /** Shift+ArrowLeft/Right on a pedal: swap it with the neighbor in `direction`. */
  onSwap: (id: string, direction: -1 | 1) => void;
  /** True for the duration between exceeding the drag threshold and the next pointerdown — check this before honoring a click. */
  didDragRef: React.MutableRefObject<boolean>;
}

/**
 * Drives the floor's drag-to-reorder: pointer-drag a pedal horizontally,
 * neighbors shift aside live, release commits the move. Keyboard
 * Shift+Arrow swaps a pedal with its neighbor the same way. The committed
 * order itself lives with the caller (Room/the engine) — this hook only
 * owns the in-flight gesture and the settle animation, and reports a
 * commit as (from, to) indices into `order` (the caller maps those to
 * slot indices for the movePedal message).
 */
export function useChainReorder(order: ChainOrder, onCommit: (fromIndex: number, toIndex: number) => void): ChainReorderApi {
  const orderRef = useRef(order);
  orderRef.current = order;

  const slotElsRef = useRef(new Map<string, HTMLElement>());
  const springsRef = useRef(new Map<string, SpringState>());
  const targetsRef = useRef(new Map<string, number>());
  const restingCentersRef = useRef<number[]>([]);
  const dragStartXRef = useRef(0);
  const dragStartYRef = useRef(0);
  const dragDxRef = useRef(0);
  const previewIndexRef = useRef(0);
  const rafRef = useRef<number | null>(null);
  const didDragRef = useRef(false);
  // Mirrors the `draggingId` state into a ref so the rAF loop (created lazily,
  // long-lived across renders) always reads the latest value without restarting.
  const draggingIdRef = useRef<string | null>(null);

  const [draggingId, setDraggingId] = useState<string | null>(null);
  const [active, setActive] = useState(false);
  const [offsets, setOffsets] = useState<Record<string, number>>({});

  const registerSlot = useCallback((id: string, el: HTMLElement | null) => {
    if (el) slotElsRef.current.set(id, el);
    else slotElsRef.current.delete(id);
  }, []);

  const measureCenters = useCallback((ids: ChainOrder): number[] => {
    return ids.map((id) => {
      const el = slotElsRef.current.get(id);
      if (!el) return 0;
      const rect = el.getBoundingClientRect();
      return rect.left + rect.width / 2;
    });
  }, []);

  const measureSlotStep = useCallback((): number => {
    const centers = measureCenters(orderRef.current);
    if (centers.length < 2) return FALLBACK_SLOT_STEP;
    let total = 0;
    for (let i = 1; i < centers.length; i++) total += centers[i] - centers[i - 1];
    return total / (centers.length - 1) || FALLBACK_SLOT_STEP;
  }, [measureCenters]);

  const ensureLoopRunning = useCallback(() => {
    if (rafRef.current !== null) return;
    let last = performance.now();

    const tick = (now: number) => {
      const dt = Math.min(0.05, Math.max(0, (now - last) / 1000));
      last = now;

      const ids = orderRef.current;
      const nextOffsets: Record<string, number> = {};
      let stillMoving = false;

      for (const id of ids) {
        if (id === draggingIdRef.current) {
          const s = { x: dragDxRef.current, v: 0 };
          springsRef.current.set(id, s);
          nextOffsets[id] = s.x;
          continue;
        }
        const target = targetsRef.current.get(id) ?? 0;
        const prev = springsRef.current.get(id) ?? freshSpring();
        const next = stepSpring(prev, target, dt, PEDAL_SPRING);
        springsRef.current.set(id, next);
        nextOffsets[id] = next.x;
        if (!isSpringSettled(next, target)) stillMoving = true;
      }

      setOffsets(nextOffsets);

      if (draggingIdRef.current !== null || stillMoving) {
        rafRef.current = requestAnimationFrame(tick);
      } else {
        rafRef.current = null;
        setActive(false);
      }
    };

    setActive(true);
    rafRef.current = requestAnimationFrame(tick);
  }, []);

  useEffect(() => {
    draggingIdRef.current = draggingId;
  }, [draggingId]);

  useEffect(
    () => () => {
      if (rafRef.current !== null) cancelAnimationFrame(rafRef.current);
    },
    [],
  );

  const applyShiftTargets = useCallback((fromIndex: number, previewIndex: number, ids: ChainOrder) => {
    targetsRef.current.clear();
    if (fromIndex === previewIndex) return;
    const step = measureSlotStep();
    ids.forEach((id, i) => {
      if (i === fromIndex) return;
      if (fromIndex < previewIndex && i > fromIndex && i <= previewIndex) {
        targetsRef.current.set(id, -step);
      } else if (fromIndex > previewIndex && i >= previewIndex && i < fromIndex) {
        targetsRef.current.set(id, step);
      }
    });
  }, [measureSlotStep]);

  const onSlotPointerDown = useCallback(
    (e: ReactPointerEvent<HTMLElement>, id: string) => {
      if (e.button !== 0) return;
      const target = e.target as HTMLElement;
      if (target.closest(".pedal-footswitch")) return; // let the footswitch handle its own click

      didDragRef.current = false; // safe to reset here: this is the *next* gesture, after any prior click already fired
      dragStartXRef.current = e.clientX;
      dragStartYRef.current = e.clientY;
      dragDxRef.current = 0;
      previewIndexRef.current = orderRef.current.indexOf(id);
      restingCentersRef.current = measureCenters(orderRef.current);

      e.currentTarget.setPointerCapture?.(e.pointerId);
      setDraggingId(id);
      draggingIdRef.current = id;
    },
    [measureCenters],
  );

  const onSlotPointerMove = useCallback(
    (e: ReactPointerEvent<HTMLElement>) => {
      const id = draggingIdRef.current;
      if (id === null) return;

      const dx = e.clientX - dragStartXRef.current;
      const dy = e.clientY - dragStartYRef.current;

      if (!didDragRef.current) {
        if (!exceedsDragThreshold(dx, dy)) return;
        didDragRef.current = true;
        ensureLoopRunning();
      }

      dragDxRef.current = dx;

      const ids = orderRef.current;
      const fromIndex = ids.indexOf(id);
      const centers = restingCentersRef.current;
      const draggedCenterX = (centers[fromIndex] ?? 0) + dx;
      const previewIndex = dragOverIndex(ids, id, draggedCenterX, centers);
      previewIndexRef.current = previewIndex;
      applyShiftTargets(fromIndex, previewIndex, ids);
    },
    [applyShiftTargets, ensureLoopRunning],
  );

  const endDrag = useCallback(() => {
    const id = draggingIdRef.current;
    if (id === null) return;

    if (didDragRef.current) {
      const fromIndex = orderRef.current.indexOf(id);
      const toIndex = previewIndexRef.current;
      if (fromIndex !== -1 && fromIndex !== toIndex) onCommit(fromIndex, toIndex);
    }

    targetsRef.current.clear();
    setDraggingId(null);
    draggingIdRef.current = null;
    ensureLoopRunning();
  }, [ensureLoopRunning, onCommit]);

  const onSlotPointerUp = useCallback(() => {
    endDrag();
  }, [endDrag]);

  const onSwap = useCallback(
    (id: string, direction: -1 | 1) => {
      const current = orderRef.current;
      const index = current.indexOf(id);
      if (index === -1) return;
      const neighborIndex = index + direction;
      if (neighborIndex < 0 || neighborIndex >= current.length) return;

      const step = measureSlotStep();
      const neighborId = current[neighborIndex];

      springsRef.current.set(id, { x: direction * step, v: 0 });
      if (neighborId) springsRef.current.set(neighborId, { x: -direction * step, v: 0 });
      targetsRef.current.set(id, 0);
      if (neighborId) targetsRef.current.set(neighborId, 0);

      onCommit(index, neighborIndex);
      ensureLoopRunning();
    },
    [ensureLoopRunning, measureSlotStep, onCommit],
  );

  return {
    offsets,
    draggingId,
    active,
    registerSlot,
    onSlotPointerDown,
    onSlotPointerMove,
    onSlotPointerUp,
    onSwap,
    didDragRef,
  };
}
