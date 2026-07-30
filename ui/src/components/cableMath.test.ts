import { describe, expect, it } from "vitest";
import { cablePath, MAX_SAG_PX, MIN_SAG_PX, sagAmount } from "./cableMath";

describe("sagAmount", () => {
  it("clamps very short spans to the minimum sag", () => {
    expect(sagAmount({ x: 0, y: 0 }, { x: 2, y: 0 })).toBe(MIN_SAG_PX);
  });

  it("clamps very long spans to the maximum sag", () => {
    expect(sagAmount({ x: 0, y: 0 }, { x: 2000, y: 0 })).toBe(MAX_SAG_PX);
  });

  it("grows with span in between", () => {
    const short = sagAmount({ x: 0, y: 0 }, { x: 150, y: 0 });
    const long = sagAmount({ x: 0, y: 0 }, { x: 250, y: 0 });
    expect(long).toBeGreaterThan(short);
  });
});

describe("cablePath", () => {
  it("starts and ends exactly at the given endpoints", () => {
    const d = cablePath({ x: 10, y: 20 }, { x: 200, y: 40 });
    expect(d.startsWith("M 10 20")).toBe(true);
    expect(d.endsWith("200 40")).toBe(true);
  });

  it("produces a cubic bezier (one M, one C, two control points)", () => {
    const d = cablePath({ x: 0, y: 0 }, { x: 100, y: 0 });
    expect(d).toMatch(/^M [\d.-]+ [\d.-]+ C [\d.-]+ [\d.-]+, [\d.-]+ [\d.-]+, [\d.-]+ [\d.-]+$/);
  });

  it("drops both control points below the straight line between the endpoints (sag, not a taut wire)", () => {
    const a = { x: 0, y: 100 };
    const b = { x: 200, y: 100 };
    const d = cablePath(a, b);
    const match = d.match(/C ([\d.-]+) ([\d.-]+), ([\d.-]+) ([\d.-]+),/);
    expect(match).not.toBeNull();
    const [, , c1y, , c2y] = match!.map(Number);
    expect(c1y).toBeGreaterThan(100);
    expect(c2y).toBeGreaterThan(100);
  });

  it("still sags for an upward-trending run (pedal to amp behind/above it)", () => {
    const a = { x: 100, y: 300 };
    const b = { x: 120, y: 50 }; // b is above a
    const d = cablePath(a, b);
    const match = d.match(/C ([\d.-]+) ([\d.-]+), ([\d.-]+) ([\d.-]+),/);
    const [, , c1y] = match!.map(Number);
    const straightMidY = (a.y + b.y) / 2;
    expect(c1y).toBeGreaterThan(straightMidY);
  });
});
