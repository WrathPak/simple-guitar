import { useEffect, useRef, useState } from "react";
import type { ParamId } from "../../../schema/gen/ts/bridge";
import { getSliderState } from "./sliderState";

/** A live handle to one of the engine's 12 host params, for use directly in JSX (Knob's value/onChange, a toggle's aria-pressed/onClick, ...). */
export interface ParamHandle {
  /** Current normalized value in [0, 1]. */
  value: number;
  /** Push a new normalized value to the backend. */
  setValue: (value: number) => void;
}

/**
 * Subscribes to one param channel (see ./sliderState.ts for the wire
 * contract) and re-renders the calling component as updates arrive, either
 * from a local gesture or a backend-originated change (host automation,
 * preset restore). `defaultValue` is only a seed for the brief window before
 * the engine's own initial push lands (or permanently, in the dev fallback).
 *
 * Every param this app cares about is subscribed once, up front, from
 * `Room` (which mounts immediately on page load) rather than lazily from
 * whichever device/overlay happens to be focused/open -- the engine pushes
 * each param's initial value exactly once, right after the page finishes
 * loading, so a listener registered later than that would miss it.
 */
export function useParam(paramId: ParamId, defaultValue = 0.5): ParamHandle {
  const slider = useRef(getSliderState(paramId, defaultValue));
  const [value, setValue] = useState(slider.current.value);

  useEffect(() => slider.current.subscribe(setValue), []);

  return { value, setValue: slider.current.setValue };
}
