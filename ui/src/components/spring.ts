/**
 * Hand-rolled 1D spring integrator used to settle pedals (and the cables
 * that follow them) into place after a drag or a keyboard swap. Slightly
 * underdamped so it settles with one small, physical overshoot — never a
 * cartoon wobble.
 */

export interface SpringState {
  x: number;
  v: number;
}

export interface SpringConfig {
  /** N/m-equivalent stiffness. Higher = snappier. */
  stiffness: number;
  /** 0..1: 1 = critically damped (no overshoot), <1 = a little bounce. */
  dampingRatio: number;
  mass?: number;
}

/** Tuned to settle in ~250-300ms with a small, physical overshoot. */
export const PEDAL_SPRING: SpringConfig = { stiffness: 300, dampingRatio: 0.82, mass: 1 };

const POSITION_EPSILON = 0.5;
/**
 * In px/s. Scaled to "sub-pixel movement per frame at 60fps" rather than a
 * near-zero absolute value — a spring settling a 100+px reorder still has
 * real velocity long after it's visually still.
 */
const VELOCITY_EPSILON = 20;

/** Advance the spring by `dt` seconds toward `target`. */
export function stepSpring(state: SpringState, target: number, dt: number, config: SpringConfig): SpringState {
  const mass = config.mass ?? 1;
  const damping = config.dampingRatio * 2 * Math.sqrt(config.stiffness * mass);
  const displacement = state.x - target;
  const force = -config.stiffness * displacement - damping * state.v;
  const acceleration = force / mass;
  const v = state.v + acceleration * dt;
  const x = state.x + v * dt;
  return { x, v };
}

/** True once the spring is close enough to `target` and nearly still. */
export function isSpringSettled(state: SpringState, target: number, config: SpringConfig = PEDAL_SPRING): boolean {
  void config;
  return Math.abs(state.x - target) < POSITION_EPSILON && Math.abs(state.v) < VELOCITY_EPSILON;
}
