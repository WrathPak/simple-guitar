import { describe, expect, it } from "vitest";
import { METER_MAX_DB, METER_MIN_DB, clampDb, dbToFraction, formatDb, nextPeakHold } from "./meterMath";

describe("clampDb", () => {
  it("clamps below the floor to the floor", () => {
    expect(clampDb(-120)).toBe(METER_MIN_DB);
  });

  it("clamps above the ceiling to the ceiling", () => {
    expect(clampDb(40)).toBe(METER_MAX_DB);
  });

  it("passes through in-range values", () => {
    expect(clampDb(-12)).toBe(-12);
  });

  it("treats NaN as the floor", () => {
    expect(clampDb(Number.NaN)).toBe(METER_MIN_DB);
  });

  it("honors custom bounds", () => {
    expect(clampDb(-100, -40, 0)).toBe(-40);
    expect(clampDb(10, -40, 0)).toBe(0);
  });
});

describe("dbToFraction", () => {
  it("maps the floor to 0 and the ceiling to 1", () => {
    expect(dbToFraction(METER_MIN_DB)).toBe(0);
    expect(dbToFraction(METER_MAX_DB)).toBe(1);
  });

  it("clamps out-of-range input before mapping", () => {
    expect(dbToFraction(METER_MIN_DB - 50)).toBe(0);
    expect(dbToFraction(METER_MAX_DB + 50)).toBe(1);
  });

  it("is linear in between", () => {
    const mid = METER_MIN_DB + (METER_MAX_DB - METER_MIN_DB) / 2;
    expect(dbToFraction(mid)).toBeCloseTo(0.5, 10);
  });
});

describe("formatDb", () => {
  it("renders -inf at or below the floor", () => {
    expect(formatDb(METER_MIN_DB)).toBe("-inf");
    expect(formatDb(-999)).toBe("-inf");
  });

  it("renders a leading + for positive values", () => {
    expect(formatDb(3)).toBe("+3.0");
  });

  it("renders negative values without a double sign", () => {
    expect(formatDb(-6)).toBe("-6.0");
  });

  it("renders non-finite input as -inf", () => {
    expect(formatDb(Number.NaN)).toBe("-inf");
    expect(formatDb(Number.POSITIVE_INFINITY)).toBe(`+${METER_MAX_DB.toFixed(1)}`);
  });
});

describe("nextPeakHold", () => {
  it("jumps up immediately to a higher incoming peak", () => {
    expect(nextPeakHold(-20, -5)).toBe(-5);
  });

  it("decays toward a lower incoming peak instead of snapping", () => {
    const held = nextPeakHold(-5, -20, 1);
    expect(held).toBe(-6);
    expect(held).toBeGreaterThan(-20);
  });

  it("never decays past the incoming peak", () => {
    expect(nextPeakHold(-5, -5.3, 10)).toBe(-5.3);
  });

  it("stays within meter bounds", () => {
    expect(nextPeakHold(METER_MAX_DB, METER_MAX_DB + 10)).toBe(METER_MAX_DB);
  });
});
