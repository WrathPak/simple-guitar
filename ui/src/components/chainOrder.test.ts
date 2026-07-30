import { describe, expect, it } from "vitest";
import { DRAG_THRESHOLD_PX, dragOverIndex, exceedsDragThreshold } from "./chainOrder";

describe("dragOverIndex", () => {
  const order = ["a", "b", "c"];
  const centers = [100, 300, 500];

  it("stays put when the drag hasn't crossed a neighbor's center", () => {
    expect(dragOverIndex(order, "a", 150, centers)).toBe(0);
  });

  it("previews moving right once the dragged center passes the next slot's center", () => {
    expect(dragOverIndex(order, "a", 320, centers)).toBe(1);
  });

  it("previews moving all the way right past two neighbors", () => {
    expect(dragOverIndex(order, "a", 520, centers)).toBe(2);
  });

  it("previews moving left symmetrically", () => {
    expect(dragOverIndex(order, "c", 280, centers)).toBe(1);
  });

  it("falls back to the current index for an unknown id", () => {
    expect(dragOverIndex(order, "z", 0, centers)).toBe(0);
  });
});

describe("exceedsDragThreshold", () => {
  it("is false for tiny jitter", () => {
    expect(exceedsDragThreshold(1, 1)).toBe(false);
  });

  it("is true once the combined motion passes the threshold", () => {
    expect(exceedsDragThreshold(DRAG_THRESHOLD_PX + 1, 0)).toBe(true);
    expect(exceedsDragThreshold(0, DRAG_THRESHOLD_PX + 1)).toBe(true);
  });

  it("uses euclidean distance, not either axis alone", () => {
    expect(exceedsDragThreshold(4, 4)).toBe(true); // hypot(4,4) ≈ 5.66 > 5
    expect(exceedsDragThreshold(3, 3)).toBe(false); // hypot(3,3) ≈ 4.24 < 5
  });
});
