/**
 * Pure geometry for the two kinds of patch cable on the floor:
 *
 * - Pedal to pedal: a short vertical stub up out of the OUT jack, a tight
 *   rounded corner into a flat-ish run above the pedal tops, a mirrored
 *   corner, then a stub straight down into the next IN jack — crisp,
 *   deliberate cable dressing, not a soft rope sagging or ballooning.
 * - Last pedal to the amp: stylized orthogonal routing — straight up,
 *   a rounded 90° corner, a clean horizontal run, another rounded corner,
 *   then straight up behind the amp — the way a pedalboard gets dressed
 *   for a photo.
 *
 * Both shapes are built from the same primitive: a straight run in, a
 * quarter-circle (approximated with a cubic bezier) rounding the corner,
 * and a straight run out.
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

// ---- Shared corner primitive -------------------------------------------

/** Cubic-bezier approximation of a quarter circle. */
const KAPPA = 0.5523;

interface Corner {
  /** Where the straight run-in ends and the curve begins. */
  entry: Point;
  /** Where the curve ends and the straight run-out begins. */
  exit: Point;
  c1: Point;
  c2: Point;
}

/**
 * A rounded corner of radius `r` at `corner`, turning from `dirIn` (unit
 * vector, the direction of travel arriving at the corner) to `dirOut` (unit
 * vector, the direction of travel leaving it).
 */
function roundedCorner(corner: Point, dirIn: Point, dirOut: Point, r: number): Corner {
  const entry: Point = { x: corner.x - dirIn.x * r, y: corner.y - dirIn.y * r };
  const exit: Point = { x: corner.x + dirOut.x * r, y: corner.y + dirOut.y * r };
  const c1: Point = { x: entry.x + dirIn.x * r * KAPPA, y: entry.y + dirIn.y * r * KAPPA };
  const c2: Point = { x: exit.x - dirOut.x * r * KAPPA, y: exit.y - dirOut.y * r * KAPPA };
  return { entry, exit, c1, c2 };
}

function cornerSegment(corner: Corner): string {
  return `C ${round(corner.c1.x)} ${round(corner.c1.y)}, ${round(corner.c2.x)} ${round(corner.c2.y)}, ${round(corner.exit.x)} ${round(corner.exit.y)}`;
}

/** Bounds a desired corner radius to whatever room the geometry actually has, with a small floor. */
function cornerRadiusFor(desired: number, ...spans: number[]): number {
  const maxAllowed = Math.max(4, Math.min(...spans.map((s) => Math.abs(s) / 2)));
  return Math.max(4, Math.min(desired, maxAllowed));
}

// ---- Pedal-to-pedal arch ------------------------------------------------

export const MIN_ARCH_HEIGHT = 18;
export const MAX_ARCH_HEIGHT = 28;
/** How much of the OUT-to-IN span becomes arch height, before clamping. */
const ARCH_HEIGHT_RATIO = 0.24;
/** Tight, crisp corner radius — deliberately smaller than the amp run's, so the turn reads sharp. */
export const ARCH_CORNER_RADIUS = 11;

function archHeightFor(span: number): number {
  return clamp(Math.abs(span) * ARCH_HEIGHT_RATIO, MIN_ARCH_HEIGHT, MAX_ARCH_HEIGHT);
}

/**
 * SVG path `d` for a pedal-to-pedal cable: a short vertical stub up from
 * `a`, a tight rounded corner into a flat run above the jack line, a
 * mirrored corner, then a stub straight down into `b`. Deliberately sharp —
 * short corners, not one big soft arch.
 */
export function pedalArchPath(a: Point, b: Point, cornerRadius: number = ARCH_CORNER_RADIUS): string {
  const dx = b.x - a.x;
  const dirX = dx >= 0 ? 1 : -1;
  const jackLine = Math.min(a.y, b.y);
  const apexY = jackLine - archHeightFor(dx);
  const r = cornerRadiusFor(cornerRadius, a.y - apexY, b.y - apexY, dx);

  // Left corner: turns from "straight up out of `a`" to "flat, toward `b`".
  const left = roundedCorner({ x: a.x, y: apexY }, { x: 0, y: -1 }, { x: dirX, y: 0 }, r);
  // Right corner: turns from "flat" to "straight down into `b`".
  const right = roundedCorner({ x: b.x, y: apexY }, { x: dirX, y: 0 }, { x: 0, y: 1 }, r);

  return [
    `M ${round(a.x)} ${round(a.y)}`,
    `L ${round(left.entry.x)} ${round(left.entry.y)}`,
    cornerSegment(left),
    `L ${round(right.entry.x)} ${round(right.entry.y)}`,
    cornerSegment(right),
    `L ${round(b.x)} ${round(b.y)}`,
  ].join(" ");
}

// ---- Last pedal to amp: orthogonal routing ------------------------------

export const DEFAULT_CORNER_RADIUS = 17;

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

  // First corner: turns from "straight up out of `out`" to "horizontal, toward `anchor`".
  const first = roundedCorner({ x: out.x, y: railY }, { x: 0, y: -1 }, { x: dirX, y: 0 }, r);
  // Second corner: turns from "horizontal" back to "straight up into `anchor`".
  const second = roundedCorner({ x: anchor.x, y: railY }, { x: dirX, y: 0 }, { x: 0, y: -1 }, r);

  return [
    `M ${round(out.x)} ${round(out.y)}`,
    `L ${round(first.entry.x)} ${round(first.entry.y)}`,
    cornerSegment(first),
    `L ${round(second.entry.x)} ${round(second.entry.y)}`,
    cornerSegment(second),
    `L ${round(anchor.x)} ${round(anchor.y)}`,
  ].join(" ");
}
