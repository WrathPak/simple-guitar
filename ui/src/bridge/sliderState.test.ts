import { beforeEach, describe, expect, it, vi } from "vitest";
import { getSliderState } from "./sliderState";

describe("getSliderState dev fallback (window.__JUCE__ absent)", () => {
  beforeEach(() => {
    // Ensure no stray __JUCE__ global from another test/module.
    delete window.__JUCE__;
  });

  it("seeds from the initial value and reads it back", () => {
    const state = getSliderState("test.seed", 0.42);
    expect(state.value).toBeCloseTo(0.42, 10);
  });

  it("setValue updates value and notifies subscribers", () => {
    const state = getSliderState("test.setValue", 0.5);
    const listener = vi.fn();
    state.subscribe(listener);

    state.setValue(0.9);

    expect(state.value).toBeCloseTo(0.9, 10);
    expect(listener).toHaveBeenCalledWith(0.9);
  });

  it("clamps out-of-range values to 0..1", () => {
    const state = getSliderState("test.clamp", 0.5);
    state.setValue(5);
    expect(state.value).toBe(1);
    state.setValue(-5);
    expect(state.value).toBe(0);
  });

  it("unsubscribe stops further notifications", () => {
    const state = getSliderState("test.unsub", 0.5);
    const listener = vi.fn();
    const unsubscribe = state.subscribe(listener);
    unsubscribe();
    state.setValue(0.8);
    expect(listener).not.toHaveBeenCalled();
  });
});
