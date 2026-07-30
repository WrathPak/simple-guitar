import { useCallback, useMemo, useRef, useState, type CSSProperties, type KeyboardEvent, type PointerEvent, type WheelEvent } from "react";
import "./Knob.css";
import { clampNormalized, dragDeltaToValue, keyDeltaToValue, scrollDeltaToValue, valueToAngle } from "./knobMath";

export interface KnobProps {
  /** Normalized value 0..1. Omit to run uncontrolled (seeded by defaultValue). */
  value?: number;
  /** Uncontrolled seed value, and the value restored on double-click. */
  defaultValue?: number;
  /** Uppercase micro-label rendered below the cap. */
  label: string;
  /** Physical cap diameter in px (58 amp faceplate, ~57 focused pedal, ...). */
  size?: number;
  /** Formats the normalized value for the drag/hover tooltip only. */
  format?: (value: number) => string;
  onChange?: (value: number) => void;
  className?: string;
}

const DEFAULT_FORMAT = (value: number) => value.toFixed(2);

export function Knob({ value, defaultValue = 0.5, label, size = 46, format = DEFAULT_FORMAT, onChange, className }: KnobProps) {
  const isControlled = value !== undefined;
  const [internalValue, setInternalValue] = useState(() => clampNormalized(defaultValue));
  const currentValue = clampNormalized(isControlled ? value! : internalValue);

  const [dragging, setDragging] = useState(false);
  const [hovering, setHovering] = useState(false);
  const dragStart = useRef<{ y: number; value: number } | null>(null);

  const commit = useCallback(
    (next: number) => {
      const clamped = clampNormalized(next);
      if (!isControlled) setInternalValue(clamped);
      onChange?.(clamped);
    },
    [isControlled, onChange],
  );

  const handlePointerDown = useCallback(
    (e: PointerEvent<HTMLDivElement>) => {
      if (e.button !== 0) return;
      e.currentTarget.setPointerCapture?.(e.pointerId);
      dragStart.current = { y: e.clientY, value: currentValue };
      setDragging(true);
    },
    [currentValue],
  );

  const handlePointerMove = useCallback(
    (e: PointerEvent<HTMLDivElement>) => {
      if (!dragStart.current) return;
      const deltaY = e.clientY - dragStart.current.y;
      commit(dragDeltaToValue(dragStart.current.value, deltaY, { fine: e.shiftKey }));
    },
    [commit],
  );

  const endDrag = useCallback((e: PointerEvent<HTMLDivElement>) => {
    if (dragStart.current && e.currentTarget.hasPointerCapture?.(e.pointerId)) {
      e.currentTarget.releasePointerCapture(e.pointerId);
    }
    dragStart.current = null;
    setDragging(false);
  }, []);

  const handleWheel = useCallback(
    (e: WheelEvent<HTMLDivElement>) => {
      e.preventDefault();
      commit(scrollDeltaToValue(currentValue, e.deltaY));
    },
    [commit, currentValue],
  );

  const handleDoubleClick = useCallback(() => {
    commit(defaultValue);
  }, [commit, defaultValue]);

  const handleKeyDown = useCallback(
    (e: KeyboardEvent<HTMLDivElement>) => {
      let direction: 1 | -1 | null = null;
      if (e.key === "ArrowUp" || e.key === "ArrowRight") direction = 1;
      else if (e.key === "ArrowDown" || e.key === "ArrowLeft") direction = -1;
      if (direction === null) return;
      e.preventDefault();
      commit(keyDeltaToValue(currentValue, direction, e.shiftKey));
    },
    [commit, currentValue],
  );

  const angle = useMemo(() => valueToAngle(currentValue), [currentValue]);
  const showTooltip = dragging || hovering;

  const capStyle = {
    "--knob-size": `${size}px`,
    "--knob-angle": `${angle}deg`,
  } as CSSProperties;

  return (
    <div className={["knob", className].filter(Boolean).join(" ")} data-size={size}>
      <div
        className="knob-cap"
        style={capStyle}
        role="slider"
        tabIndex={0}
        aria-label={label}
        aria-valuemin={0}
        aria-valuemax={1}
        aria-valuenow={currentValue}
        aria-valuetext={format(currentValue)}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={endDrag}
        onPointerCancel={endDrag}
        onWheel={handleWheel}
        onDoubleClick={handleDoubleClick}
        onKeyDown={handleKeyDown}
        onMouseEnter={() => setHovering(true)}
        onMouseLeave={() => setHovering(false)}
      >
        <div className="knob-pointer" />
        {showTooltip && (
          <div className="knob-tooltip" role="status">
            {format(currentValue)}
          </div>
        )}
      </div>
      <div className="knob-label">{label}</div>
    </div>
  );
}
