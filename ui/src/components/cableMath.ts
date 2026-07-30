/**
 * Pure geometry for the two kinds of patch cable on the floor:
 *
 * - Pedal to pedal: exits the OUT jack going straight up, arches proudly
 *   above the pedal tops, and drops straight down into the next IN jack —
 *   a cable dressed in a deliberate loop, not a wire sagging under gravity.
 * - Last pedal to the amp: stylized orthogonal routing — straight up,
 *   a rounded 90° corner, a clean horizontal run, another rounded corner,
 *   then straight up behind the amp — the way a pedalboard gets dressed
 *   for a photo.
 */

export interface Point {
  x: number;
  y: number;
}

/** A subset of DOMRect — plain-object friendly, so this stays unit-testable without a real DOM. */
export interface RectLike {
  left: number;
  top: number;
  width: number;
  height: number;
}

/**
 * Where a cable should visibly enter a port: its top-center, so the cable
 * runs straight down (or up) into the socket rather than off to one side of
 * it. `portRect` and `containerRect` are both viewport-relative; the result
 * is relative to `containerRect`.
 */
export function portTopCenter(portRect: RectLike, containerRect: RectLike): Point {
  return {
    x: portRect.left + portRect.width / 2 - containerRect.left,
    y: portRect.top - containerRect.top,
  };
}

function round(n: number): number {
  return Math.round(n * 100) / 100;
}

function clamp(n: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, n));
}

// ---- Pedal-to-pedal arch ----------------------------------------------

export const MIN_ARCH_HEIGHT = 30;
export const MAX_ARCH_HEIGHT = 45;
/** How much of the peak-to-peak span becomes arch height, before clamping. */
const ARCH_HEIGHT_RATIO = 0.3;
/** How flat the top of the arch is: half-width of the flat span, before clamping. */
const ARCH_SPREAD_RATIO = 0.2;
const MAX_ARCH_SPREAD = 36;

function archHeightFor(span: number): number {
  return clamp(Math.abs(span) * ARCH_HEIGHT_RATIO, MIN_ARCH_HEIGHT, MAX_ARCH_HEIGHT);
}

function archSpreadFor(span: number): number {
  return Math.min(Math.abs(span) * ARCH_SPREAD_RATIO, MAX_ARCH_SPREAD);
}

/**
 * SVG path `d` for a pedal-to-pedal cable: a short vertical stub up from
 * `a`, a flat-topped rounded arch above the jack line, then a stub straight
 * down into `b`. Two cubic segments sharing a horizontal tangent at the
 * apex, so the join is smooth.
 */
export function pedalArchPath(a: Point, b: Point): string {
  const dx = b.x - a.x;
  const midX = (a.x + b.x) / 2;
  const jackLine = Math.min(a.y, b.y);
  const apexY = jackLine - archHeightFor(dx);
  const spread = archSpreadFor(dx);

  const upCtrl: Point = { x: a.x, y: apexY };
  const preApex: Point = { x: midX - spread, y: apexY };
  const apex: Point = { x: midX, y: apexY };
  const postApex: Point = { x: midX + spread, y: apexY };
  const downCtrl: Point = { x: b.x, y: apexY };

  return [
    `M ${round(a.x)} ${round(a.y)}`,
    `C ${round(upCtrl.x)} ${round(upCtrl.y)}, ${round(preApex.x)} ${round(preApex.y)}, ${round(apex.x)} ${round(apex.y)}`,
    `C ${round(postApex.x)} ${round(postApex.y)}, ${round(downCtrl.x)} ${round(downCtrl.y)}, ${round(b.x)} ${round(b.y)}`,
  ].join(" ");
}

// ---- Last pedal to amp: orthogonal routing ----------------------------

export const DEFAULT_CORNER_RADIUS = 17;
/** Cubic-bezier approximation of a quarter circle. */
const KAPPA = 0.5523;

function cornerRadiusFor(desired: number, ...spans: number[]): number {
  const maxAllowed = Math.max(4, Math.min(...spans.map((s) => Math.abs(s) / 2)));
  return Math.max(4, Math.min(desired, maxAllowed));
}

/**
 * SVG path `d` for the last pedal's cable up to the amp: straight up from
 * `out`, a rounded 90° corner, a horizontal run at a height between the
 * pedal row and the anchor, another rounded 90° corner, then straight up
 * into `anchor` (which sits tucked behind the amp head).
 */
export function ampRoutePath(out: Point, anchor: Point, cornerRadius: number = DEFAULT_CORNER_RADIUS): string {
  const dirX = anchor.x >= out.x ? 1 : -1;
  const railY = (out.y + anchor.y) / 2;
  const r = cornerRadiusFor(cornerRadius, out.y - railY, railY - anchor.y, anchor.x - out.x);

  const p1: Point = { x: out.x, y: railY + r };
  const p2: Point = { x: out.x + dirX * r, y: railY };
  const c1a: Point = { x: out.x, y: railY + r - r * KAPPA };
  const c1b: Point = { x: out.x + dirX * r * (1 - KAPPA), y: railY };

  const p3: Point = { x: anchor.x - dirX * r, y: railY };
  const p4: Point = { x: anchor.x, y: railY - r };
  const c2a: Point = { x: anchor.x - dirX * r * (1 - KAPPA), y: railY };
  const c2b: Point = { x: anchor.x, y: railY - r + r * KAPPA };

  return [
    `M ${round(out.x)} ${round(out.y)}`,
    `L ${round(p1.x)} ${round(p1.y)}`,
    `C ${round(c1a.x)} ${round(c1a.y)}, ${round(c1b.x)} ${round(c1b.y)}, ${round(p2.x)} ${round(p2.y)}`,
    `L ${round(p3.x)} ${round(p3.y)}`,
    `C ${round(c2a.x)} ${round(c2a.y)}, ${round(c2b.x)} ${round(c2b.y)}, ${round(p4.x)} ${round(p4.y)}`,
    `L ${round(anchor.x)} ${round(anchor.y)}`,
  ].join(" ");
}
