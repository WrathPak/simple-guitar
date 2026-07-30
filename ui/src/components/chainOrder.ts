/**
 * Pure logic for the floor's visual order: the left-to-right list of slot
 * ids that also doubles as signal order (last pedal feeds the amp). Kept
 * dependency-free and side-effect-free so drag-over index math can be unit
 * tested directly. The committed move itself is just a (from, to) index
 * pair — the engine owns applying it (the movePedal message), so there's
 * no client-side reorder function anymore.
 */

export type ChainOrder = string[];

/**
 * Given a dragged id's current live center-x and every slot's resting
 * center-x (in the same left-to-right order as `order`), find the index the
 * drag should preview into: the slot whose center the drag point has
 * crossed. Returns the dragged id's own current index if it hasn't crossed
 * a neighbor yet.
 */
export function dragOverIndex(order: ChainOrder, draggedId: string, draggedCenterX: number, slotCentersX: number[]): number {
  const fromIndex = order.indexOf(draggedId);
  if (fromIndex === -1 || slotCentersX.length !== order.length) return Math.max(fromIndex, 0);

  let target = fromIndex;
  for (let i = 0; i < slotCentersX.length; i++) {
    if (i === fromIndex) continue;
    const crossedRightward = i > fromIndex && draggedCenterX > slotCentersX[i];
    const crossedLeftward = i < fromIndex && draggedCenterX < slotCentersX[i];
    if (crossedRightward) target = Math.max(target, i);
    if (crossedLeftward) target = Math.min(target, i);
  }
  return target;
}

/** Drag-vs-click gesture threshold: only treat pointer motion past this as a drag. */
export const DRAG_THRESHOLD_PX = 5;

export function exceedsDragThreshold(dx: number, dy: number, threshold: number = DRAG_THRESHOLD_PX): boolean {
  return Math.hypot(dx, dy) > threshold;
}
