import { describe, expect, it } from "vitest";
import {
  ampRoutePath,
  ARCH_CORNER_RADIUS,
  DEFAULT_CORNER_RADIUS,
  MAX_ARCH_HEIGHT,
  MIN_ARCH_HEIGHT,
  pedalArchPath,
  portTopCenter,
} from "./cableMath";

/** Mirrors cableMath's own (unexported) arch-height formula, to independently verify its output. */
function archHeightForSpan(span: number): number {
  return Math.min(MAX_ARCH_HEIGHT, Math.max(MIN_ARCH_HEIGHT, Math.abs(span) * 0.24));
}

describe("portTopCenter", () => {
  it("is horizontally centered on the port", () => {
    const port = { left: 100, top: 50, width: 20, height: 10 };
    const container = { left: 0, top: 0, width: 1000, height: 1000 };
    expect(portTopCenter(port, container)).toEqual({ x: 110, y: 50 });
  });

  it("uses the port's top edge, not its vertical center", () => {
    const port = { left: 0, top: 200, width: 10, height: 30 };
    const container = { left: 0, top: 0, width: 1000, height: 1000 };
    const p = portTopCenter(port, container);
    expect(p.y).toBe(200); // not 215 (the port's own vertical center)
  });

  it("is relative to the container's origin, not the viewport", () => {
    const port = { left: 340, top: 260, width: 10, height: 10 };
    const container = { left: 300, top: 200, width: 800, height: 600 };
    expect(portTopCenter(port, container)).toEqual({ x: 45, y: 60 });
  });
});

describe("pedalArchPath", () => {
  it("starts and ends exactly at the given jack points", () => {
    const d = pedalArchPath({ x: 10, y: 600 }, { x: 200, y: 600 });
    expect(d.startsWith("M 10 600")).toBe(true);
    expect(d.endsWith("200 600")).toBe(true);
  });

  it("is a sharp turn, not a soft arch: straight stub, tight corner, flat run, tight corner, straight stub (3 L, 2 C)", () => {
    const d = pedalArchPath({ x: 0, y: 600 }, { x: 150, y: 600 });
    const lCount = (d.match(/(?:^|\s)L\s/g) ?? []).length;
    const cCount = (d.match(/(?:^|\s)C\s/g) ?? []).length;
    expect(lCount).toBe(3);
    expect(cCount).toBe(2);
  });

  it("keeps a straight vertical stub exiting each jack before the corner starts", () => {
    const a = { x: 40, y: 600 };
    const b = { x: 260, y: 600 };
    const d = pedalArchPath(a, b);
    // First L = top of the straight stub out of `a` (still directly above it, corner hasn't started).
    const lMatches = [...d.matchAll(/L ([\d.-]+) ([\d.-]+)/g)].map((m) => ({ x: Number(m[1]), y: Number(m[2]) }));
    expect(lMatches[0].x).toBeCloseTo(a.x, 5);
    expect(lMatches[0].y).toBeLessThan(a.y);
    // The second C command's endpoint is where the corner finishes turning back to vertical,
    // directly above `b` — that's the top of the straight stub down into `b`.
    const secondCorner = [...d.matchAll(/C [\d.-]+ [\d.-]+, [\d.-]+ [\d.-]+, ([\d.-]+) ([\d.-]+)/g)][1];
    const stubIntoB = { x: Number(secondCorner[1]), y: Number(secondCorner[2]) };
    expect(stubIntoB.x).toBeCloseTo(b.x, 5);
    expect(stubIntoB.y).toBeLessThan(b.y);
  });

  it("arches within the 18-28px design range for a normal pedal gap (lower and tighter than before)", () => {
    const a = { x: 0, y: 600 };
    const b = { x: 140, y: 600 }; // typical OUT-to-next-IN gap
    const d = pedalArchPath(a, b);
    const jackLine = Math.min(a.y, b.y);
    // The flat run's height is the corner's exit point — the 2nd coordinate pair of the first C command.
    const cMatch = d.match(/C [\d.-]+ [\d.-]+, ([\d.-]+) ([\d.-]+),/)!;
    const flatY = Number(cMatch[2]);
    const archHeight = jackLine - flatY;
    expect(archHeight).toBeGreaterThanOrEqual(MIN_ARCH_HEIGHT);
    expect(archHeight).toBeLessThanOrEqual(MAX_ARCH_HEIGHT);
  });

  it("clamps arch height for a very short span to the minimum", () => {
    const d = pedalArchPath({ x: 0, y: 600 }, { x: 4, y: 600 });
    const cMatch = d.match(/C [\d.-]+ [\d.-]+, ([\d.-]+) ([\d.-]+),/)!;
    const flatY = Number(cMatch[2]);
    // A 4px span can't fit two 11px corner radii either, so this also exercises the radius clamp —
    // the important thing is the arch height itself still respects the minimum.
    expect(600 - flatY).toBeCloseTo(MIN_ARCH_HEIGHT, 5);
  });

  it("clamps arch height for a very long span to the maximum", () => {
    const d = pedalArchPath({ x: 0, y: 600 }, { x: 900, y: 600 });
    const cMatch = d.match(/C [\d.-]+ [\d.-]+, ([\d.-]+) ([\d.-]+),/)!;
    const flatY = Number(cMatch[2]);
    expect(600 - flatY).toBeCloseTo(MAX_ARCH_HEIGHT, 5);
  });

  it("still arches above the jack line when the two jacks sit at slightly different heights", () => {
    const a = { x: 0, y: 590 };
    const b = { x: 150, y: 610 };
    const d = pedalArchPath(a, b);
    const cMatch = d.match(/C [\d.-]+ [\d.-]+, ([\d.-]+) ([\d.-]+),/)!;
    const flatY = Number(cMatch[2]);
    expect(flatY).toBeLessThan(Math.min(a.y, b.y));
  });

  it("turns sharply: the default corner radius is much tighter than the amp run's", () => {
    expect(ARCH_CORNER_RADIUS).toBeLessThan(DEFAULT_CORNER_RADIUS);
  });

  it("uses the requested (tight) corner radius when there's room for it", () => {
    const a = { x: 0, y: 600 };
    const b = { x: 200, y: 600 };
    const d = pedalArchPath(a, b);
    const lMatches = [...d.matchAll(/L ([\d.-]+) ([\d.-]+)/g)].map((m) => Number(m[2]));
    const jackLine = 600;
    const flatY = jackLine - archHeightForSpan(200);
    // The stub's top (end of the first L) sits exactly `cornerRadius` short of the flat run.
    expect(lMatches[0] - flatY).toBeCloseTo(ARCH_CORNER_RADIUS, 5);
  });

  it("keeps the corners tighter than the amp run's when both have room, so pedal cables read as sharper", () => {
    const pedal = pedalArchPath({ x: 0, y: 600 }, { x: 300, y: 600 });
    const amp = ampRoutePath({ x: 0, y: 600 }, { x: 300, y: 300 });
    const pedalStubLength = (() => {
      const l = [...pedal.matchAll(/L ([\d.-]+) ([\d.-]+)/g)].map((m) => Number(m[2]));
      return 600 - l[0];
    })();
    const ampStubLength = (() => {
      const l = [...amp.matchAll(/L ([\d.-]+) ([\d.-]+)/g)].map((m) => Number(m[2]));
      return 600 - l[0];
    })();
    expect(pedalStubLength).toBeLessThan(ampStubLength);
  });
});

describe("ampRoutePath", () => {
  it("starts at the OUT jack and ends at the anchor behind the amp", () => {
    const out = { x: 700, y: 600 };
    const anchor = { x: 500, y: 350 };
    const d = ampRoutePath(out, anchor);
    expect(d.startsWith("M 700 600")).toBe(true);
    expect(d.endsWith("500 350")).toBe(true);
  });

  it("is orthogonal routing: straight-up, rounded corner, straight-across, rounded corner, straight-up (3 L, 2 C)", () => {
    const d = ampRoutePath({ x: 700, y: 600 }, { x: 500, y: 350 });
    const lCount = (d.match(/(?:^|\s)L\s/g) ?? []).length;
    const cCount = (d.match(/(?:^|\s)C\s/g) ?? []).length;
    expect(lCount).toBe(3);
    expect(cCount).toBe(2);
  });

  it("runs horizontally at a height between the OUT jack and the anchor", () => {
    const out = { x: 700, y: 600 };
    const anchor = { x: 500, y: 350 };
    const d = ampRoutePath(out, anchor);
    const lMatches = [...d.matchAll(/L ([\d.-]+) ([\d.-]+)/g)].map((m) => ({ x: Number(m[1]), y: Number(m[2]) }));
    // lMatches[0] = top of the first vertical stub (just short of the rail), lMatches[1] = far end of the horizontal run.
    const railY = (out.y + anchor.y) / 2;
    expect(lMatches[1].y).toBeCloseTo(railY, 5);
    expect(lMatches[1].y).toBeLessThan(out.y);
    expect(lMatches[1].y).toBeGreaterThan(anchor.y);
    expect(lMatches[0].y).toBeGreaterThan(lMatches[1].y); // still below the rail, approaching the first corner
  });

  it("turns toward the anchor's side: rightward when the anchor is to the right", () => {
    const out = { x: 300, y: 600 };
    const anchor = { x: 600, y: 350 };
    const d = ampRoutePath(out, anchor);
    const lMatches = [...d.matchAll(/L ([\d.-]+) ([\d.-]+)/g)].map((m) => Number(m[1]));
    expect(lMatches[0]).toBeCloseTo(out.x, 5); // top of first stub still directly above `out`
    expect(lMatches[1]).toBeGreaterThan(out.x); // far end of the horizontal run has moved toward the anchor
  });

  it("turns leftward when the anchor is to the left", () => {
    const out = { x: 600, y: 600 };
    const anchor = { x: 300, y: 350 };
    const d = ampRoutePath(out, anchor);
    const lMatches = [...d.matchAll(/L ([\d.-]+) ([\d.-]+)/g)].map((m) => Number(m[1]));
    expect(lMatches[1]).toBeLessThan(out.x);
  });

  it("stays finite even for a huge requested radius on a cramped run", () => {
    const out = { x: 500, y: 600 };
    const anchor = { x: 520, y: 590 };
    const d = ampRoutePath(out, anchor, 500);
    const coords = d.match(/-?[\d.]+/g)!.map(Number);
    expect(coords.every((n) => Number.isFinite(n))).toBe(true);
  });

  it("uses the requested corner radius when there's room for it (14-20px design range)", () => {
    const out = { x: 700, y: 700 };
    const anchor = { x: 400, y: 300 };
    const d = ampRoutePath(out, anchor, DEFAULT_CORNER_RADIUS);
    const lMatches = [...d.matchAll(/L ([\d.-]+) ([\d.-]+)/g)].map((m) => Number(m[2]));
    const railY = (out.y + anchor.y) / 2;
    // First L ends `cornerRadius` short of the rail (coming from below, y decreases upward).
    expect(lMatches[0] - railY).toBeCloseTo(DEFAULT_CORNER_RADIUS, 5);
  });
});
