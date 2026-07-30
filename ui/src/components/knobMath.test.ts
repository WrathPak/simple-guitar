import { describe, expect, it } from "vitest";
import {
  DEFAULT_DRAG_RANGE_PX,
  FINE_DRAG_MULTIPLIER,
  KNOB_MAX_ANGLE,
  KNOB_MIN_ANGLE,
  angleToValue,
  clampNormalized,
  dragDeltaToValue,
  keyDeltaToValue,
  scrollDeltaToValue,
  valueToAngle,
} from "./knobMath";

describe("clampNormalized", () => {
  it("clamps below 0 and above 1", () => {
    expect(clampNormalized(-0.5)).toBe(0);
    expect(clampNormalized(1.5)).toBe(1);
  });

  it("passes through in-range values", () => {
    expect(clampNormalized(0.42)).toBe(0.42);
  });

  it("treats NaN as 0", () => {
    expect(clampNormalized(Number.NaN)).toBe(0);
  });
});

describe("valueToAngle / angleToValue", () => {
  it("maps the extremes to -135 and +135 degrees", () => {
    expect(valueToAngle(0)).toBe(KNOB_MIN_ANGLE);
    expect(valueToAngle(1)).toBe(KNOB_MAX_ANGLE);
  });

  it("maps the midpoint to 0 degrees", () => {
    expect(valueToAngle(0.5)).toBe(0);
  });

  it("clamps out-of-range normalized values before mapping", () => {
    expect(valueToAngle(-1)).toBe(KNOB_MIN_ANGLE);
    expect(valueToAngle(2)).toBe(KNOB_MAX_ANGLE);
  });

  it("is the inverse of angleToValue across the sweep", () => {
    for (const value of [0, 0.1, 0.25, 0.5, 0.75, 0.9, 1]) {
      const angle = valueToAngle(value);
      expect(angleToValue(angle)).toBeCloseTo(value, 10);
    }
  });

  it("clamps angles outside -135..135 before mapping back to 0..1", () => {
    expect(angleToValue(KNOB_MIN_ANGLE - 50)).toBe(0);
    expect(angleToValue(KNOB_MAX_ANGLE + 50)).toBe(1);
  });
});

describe("dragDeltaToValue", () => {
  it("dragging up (negative deltaY) increases the value", () => {
    const next = dragDeltaToValue(0.5, -DEFAULT_DRAG_RANGE_PX / 4);
    expect(next).toBeCloseTo(0.75, 10);
  });

  it("dragging down (positive deltaY) decreases the value", () => {
    const next = dragDeltaToValue(0.5, DEFAULT_DRAG_RANGE_PX / 4);
    expect(next).toBeCloseTo(0.25, 10);
  });

  it("a full sweep of dragRangePx moves from 0 to 1", () => {
    expect(dragDeltaToValue(0, -DEFAULT_DRAG_RANGE_PX)).toBeCloseTo(1, 10);
    expect(dragDeltaToValue(1, DEFAULT_DRAG_RANGE_PX)).toBeCloseTo(0, 10);
  });

  it("fine mode (shift) requires FINE_DRAG_MULTIPLIER times the pixels for the same delta", () => {
    const normal = dragDeltaToValue(0.5, -50);
    const fine = dragDeltaToValue(0.5, -50, { fine: true });
    expect(fine - 0.5).toBeCloseTo((normal - 0.5) / FINE_DRAG_MULTIPLIER, 10);
  });

  it("clamps at the rails", () => {
    expect(dragDeltaToValue(0.9, -10_000)).toBe(1);
    expect(dragDeltaToValue(0.1, 10_000)).toBe(0);
  });
});

describe("scrollDeltaToValue", () => {
  it("scrolling down decreases the value by one fine step", () => {
    const next = scrollDeltaToValue(0.5, 100);
    expect(next).toBeLessThan(0.5);
  });

  it("scrolling up increases the value by one fine step", () => {
    const next = scrollDeltaToValue(0.5, -100);
    expect(next).toBeGreaterThan(0.5);
  });

  it("a zero delta is a no-op", () => {
    expect(scrollDeltaToValue(0.5, 0)).toBe(0.5);
  });

  it("respects a custom step size", () => {
    expect(scrollDeltaToValue(0.5, -1, 0.1)).toBeCloseTo(0.6, 10);
  });

  it("clamps at the rails", () => {
    expect(scrollDeltaToValue(0.999, -1, 0.5)).toBe(1);
    expect(scrollDeltaToValue(0.001, 1, 0.5)).toBe(0);
  });
});

describe("keyDeltaToValue", () => {
  it("increases on direction 1 and decreases on direction -1", () => {
    expect(keyDeltaToValue(0.5, 1, false)).toBeGreaterThan(0.5);
    expect(keyDeltaToValue(0.5, -1, false)).toBeLessThan(0.5);
  });

  it("fine mode takes a smaller step than normal mode", () => {
    const normalStep = keyDeltaToValue(0.5, 1, false) - 0.5;
    const fineStep = keyDeltaToValue(0.5, 1, true) - 0.5;
    expect(fineStep).toBeLessThan(normalStep);
    expect(fineStep).toBeGreaterThan(0);
  });

  it("clamps at the rails", () => {
    expect(keyDeltaToValue(1, 1, false)).toBe(1);
    expect(keyDeltaToValue(0, -1, false)).toBe(0);
  });
});
