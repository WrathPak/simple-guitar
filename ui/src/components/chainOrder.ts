/**
 * Pure logic for the floor's chain order: the left-to-right list of pedal
 * ids that also doubles as signal order (last pedal feeds the amp). Kept
 * dependency-free and side-effect-free so drag-over index math and the
 * keyboard swap can be unit tested directly.
 *
 * Shape note: the order is a plain string[] of pedal ids so a later bridge
 * message (once the real audio chain lands) can carry it as-is, e.g.
 * `{ type: "chainOrderChanged", order }` — nothing here assumes a UI-only
 * shape that would need reshaping later.
 */

export type ChainOrder = string[];

/** Move the id at `fromIndex` to `toIndex`, shifting the rest to make room. */
export function moveInOrder(order: ChainOrder, fromIndex: number, toIndex: number): ChainOrder {
  if (fromIndex === toIndex) return order;
  if (fromIndex < 0 || fromIndex >= order.length) return order;
  const clampedTo = Math.min(Math.max(toIndex, 0), order.length - 1);
  const next = order.slice();
  const [moved] = next.splice(fromIndex, 1);
  next.splice(clampedTo, 0, moved);
  return next;
}

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

/** Shift+ArrowLeft/Right: swap `id` with its previous/next neighbor. */
export function swapWithNeighbor(order: ChainOrder, id: string, direction: -1 | 1): ChainOrder {
  const index = order.indexOf(id);
  if (index === -1) return order;
  const target = index + direction;
  if (target < 0 || target >= order.length) return order;
  return moveInOrder(order, index, target);
}

/** Drag-vs-click gesture threshold: only treat pointer motion past this as a drag. */
export const DRAG_THRESHOLD_PX = 5;

export function exceedsDragThreshold(dx: number, dy: number, threshold: number = DRAG_THRESHOLD_PX): boolean {
  return Math.hypot(dx, dy) > threshold;
}
