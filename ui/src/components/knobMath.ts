/**
 * Pure math for the physical Knob: normalized value (0..1) <-> pointer
 * rotation angle (-135deg..+135deg), plus the gesture math
 * for drag/scroll/keyboard input. Kept dependency-free and side-effect-free
 * so it can be unit tested directly.
 */

export const KNOB_MIN_ANGLE = -135;
export const KNOB_MAX_ANGLE = 135;
export const KNOB_ANGLE_RANGE = KNOB_MAX_ANGLE - KNOB_MIN_ANGLE; // 270

export function clampNormalized(value: number): number {
  if (Number.isNaN(value)) return 0;
  return Math.min(1, Math.max(0, value));
}

/** normalized value (0..1) -> pointer rotation in degrees (-135..+135). */
export function valueToAngle(value: number): number {
  return KNOB_MIN_ANGLE + clampNormalized(value) * KNOB_ANGLE_RANGE;
}

/** pointer rotation in degrees (-135..+135, clamped) -> normalized value (0..1). */
export function angleToValue(angle: number): number {
  const clampedAngle = Math.min(KNOB_MAX_ANGLE, Math.max(KNOB_MIN_ANGLE, angle));
  return clampNormalized((clampedAngle - KNOB_MIN_ANGLE) / KNOB_ANGLE_RANGE);
}

/** px of vertical drag for a full 0..1 sweep at normal (non-fine) sensitivity. */
export const DEFAULT_DRAG_RANGE_PX = 200;
/** Shift = fine: this many times more pixels needed for the same value change. */
export const FINE_DRAG_MULTIPLIER = 4;

export interface DragToValueOptions {
  fine?: boolean;
  dragRangePx?: number;
}

/**
 * Vertical drag gesture -> new normalized value. Dragging up (negative
 * deltaY, since screen Y grows downward) increases the value.
 */
export function dragDeltaToValue(startValue: number, deltaY: number, options: DragToValueOptions = {}): number {
  const dragRangePx = options.dragRangePx ?? DEFAULT_DRAG_RANGE_PX;
  const effectiveRange = options.fine ? dragRangePx * FINE_DRAG_MULTIPLIER : dragRangePx;
  const deltaValue = -deltaY / effectiveRange;
  return clampNormalized(startValue + deltaValue);
}

/** Fine step applied per wheel notch ("scroll = fine steps"). */
export const SCROLL_STEP = 0.01;

/** Wheel scroll -> new normalized value. Scrolling down (deltaY > 0) decreases the value. */
export function scrollDeltaToValue(currentValue: number, deltaY: number, step: number = SCROLL_STEP): number {
  if (deltaY === 0) return clampNormalized(currentValue);
  const direction = deltaY > 0 ? -1 : 1;
  return clampNormalized(currentValue + direction * step);
}

export const KEY_STEP = 0.02;
export const KEY_STEP_FINE = 0.004;

/** Arrow-key adjustment -> new normalized value. */
export function keyDeltaToValue(currentValue: number, direction: 1 | -1, fine: boolean): number {
  const step = fine ? KEY_STEP_FINE : KEY_STEP;
  return clampNormalized(currentValue + direction * step);
}
