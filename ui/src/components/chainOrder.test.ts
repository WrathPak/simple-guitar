import { describe, expect, it } from "vitest";
import { DRAG_THRESHOLD_PX, dragOverIndex, exceedsDragThreshold, moveInOrder, swapWithNeighbor } from "./chainOrder";

describe("moveInOrder", () => {
  it("moves an id later, shifting the ones in between left", () => {
    expect(moveInOrder(["a", "b", "c"], 0, 2)).toEqual(["b", "c", "a"]);
  });

  it("moves an id earlier, shifting the ones in between right", () => {
    expect(moveInOrder(["a", "b", "c"], 2, 0)).toEqual(["c", "a", "b"]);
  });

  it("is a no-op when from and to are the same", () => {
    const order = ["a", "b", "c"];
    expect(moveInOrder(order, 1, 1)).toBe(order);
  });

  it("clamps an out-of-range target index", () => {
    expect(moveInOrder(["a", "b", "c"], 0, 99)).toEqual(["b", "c", "a"]);
    expect(moveInOrder(["a", "b", "c"], 2, -5)).toEqual(["c", "a", "b"]);
  });

  it("ignores an out-of-range source index", () => {
    const order = ["a", "b", "c"];
    expect(moveInOrder(order, 7, 0)).toBe(order);
  });
});

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

describe("swapWithNeighbor", () => {
  it("swaps right", () => {
    expect(swapWithNeighbor(["a", "b", "c"], "a", 1)).toEqual(["b", "a", "c"]);
  });

  it("swaps left", () => {
    expect(swapWithNeighbor(["a", "b", "c"], "c", -1)).toEqual(["a", "c", "b"]);
  });

  it("is a no-op at the left edge moving left", () => {
    const order = ["a", "b", "c"];
    expect(swapWithNeighbor(order, "a", -1)).toBe(order);
  });

  it("is a no-op at the right edge moving right", () => {
    const order = ["a", "b", "c"];
    expect(swapWithNeighbor(order, "c", 1)).toBe(order);
  });

  it("is a no-op for an unknown id", () => {
    const order = ["a", "b", "c"];
    expect(swapWithNeighbor(order, "z", 1)).toBe(order);
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
