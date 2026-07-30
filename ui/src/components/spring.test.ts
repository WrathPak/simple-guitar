import { describe, expect, it } from "vitest";
import { isSpringSettled, PEDAL_SPRING, stepSpring, type SpringState } from "./spring";

function simulate(target: number, ms: number, startX = 0): SpringState {
  let state: SpringState = { x: startX, v: 0 };
  const dt = 1 / 120;
  for (let t = 0; t < ms / 1000; t += dt) {
    state = stepSpring(state, target, dt, PEDAL_SPRING);
  }
  return state;
}

describe("stepSpring", () => {
  it("moves toward the target", () => {
    const state = stepSpring({ x: 0, v: 0 }, 100, 1 / 60, PEDAL_SPRING);
    expect(state.x).toBeGreaterThan(0);
    expect(state.x).toBeLessThan(100);
  });

  it("does nothing when already at rest on target", () => {
    const state = stepSpring({ x: 50, v: 0 }, 50, 1 / 60, PEDAL_SPRING);
    expect(state.x).toBeCloseTo(50, 5);
    expect(state.v).toBeCloseTo(0, 5);
  });

  it("settles near the target well within 300ms", () => {
    const state = simulate(120, 300);
    expect(Math.abs(state.x - 120)).toBeLessThan(1);
  });

  it("settles by the isSpringSettled check within 350ms", () => {
    let state: SpringState = { x: 0, v: 0 };
    const dt = 1 / 120;
    let settledAt = -1;
    for (let i = 0; i < 120; i++) {
      state = stepSpring(state, 80, dt, PEDAL_SPRING);
      if (isSpringSettled(state, 80)) {
        settledAt = i * dt * 1000;
        break;
      }
    }
    expect(settledAt).toBeGreaterThan(0);
    expect(settledAt).toBeLessThan(350);
  });

  it("overshoots only a small, physical amount once, not a cartoon wobble", () => {
    let state: SpringState = { x: 0, v: 0 };
    const dt = 1 / 240;
    const target = 100;
    let peakOvershoot = 0;
    for (let i = 0; i < 240 * 2; i++) {
      state = stepSpring(state, target, dt, PEDAL_SPRING);
      peakOvershoot = Math.max(peakOvershoot, state.x - target);
    }
    // A single small overshoot is the physical "settle" this spring is tuned
    // for — bounded well under 10% of the distance traveled.
    expect(peakOvershoot).toBeGreaterThan(0);
    expect(peakOvershoot).toBeLessThan(target * 0.1);
  });
});

describe("isSpringSettled", () => {
  it("is false while still moving even if close", () => {
    expect(isSpringSettled({ x: 99.9, v: 50 }, 100)).toBe(false);
  });

  it("is true once close and slow", () => {
    expect(isSpringSettled({ x: 100.1, v: 0.5 }, 100)).toBe(true);
  });
});
