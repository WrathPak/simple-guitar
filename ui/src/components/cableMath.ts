/**
 * Pure geometry for a patch cable drawn between two jack points: a cubic
 * bezier with its control points dropped below the straight line between
 * the endpoints, so it reads as a length of real cable sagging under its
 * own weight rather than a straight wire.
 */

export interface Point {
  x: number;
  y: number;
}

export const MIN_SAG_PX = 16;
export const MAX_SAG_PX = 64;
/** Sag grows with span, but is clamped so short and long runs both look right. */
export const SAG_RATIO = 0.16;

export function sagAmount(a: Point, b: Point): number {
  const span = Math.hypot(b.x - a.x, b.y - a.y);
  return Math.min(MAX_SAG_PX, Math.max(MIN_SAG_PX, span * SAG_RATIO));
}

/** SVG path `d` string for a sagging cable from `a` to `b`. */
export function cablePath(a: Point, b: Point): string {
  const sag = sagAmount(a, b);
  const midY = (a.y + b.y) / 2 + sag;
  const c1: Point = { x: a.x + (b.x - a.x) * 0.28, y: midY };
  const c2: Point = { x: a.x + (b.x - a.x) * 0.72, y: midY };
  return `M ${round(a.x)} ${round(a.y)} C ${round(c1.x)} ${round(c1.y)}, ${round(c2.x)} ${round(c2.y)}, ${round(b.x)} ${round(b.y)}`;
}

function round(n: number): number {
  return Math.round(n * 100) / 100;
}
