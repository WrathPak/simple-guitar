import { useCallback, useEffect, useRef, useState } from "react";
import "./CableLayer.css";
import { ampRoutePath, pedalArchPath, portTopCenter } from "./cableMath";

export interface JackPair {
  inEl: HTMLElement | null;
  outEl: HTMLElement | null;
}

export interface CableLayerProps {
  /** The scene element cable coordinates are measured relative to. */
  containerRef: React.RefObject<HTMLElement | null>;
  /** Pedal ids, left to right, signal order. */
  order: string[];
  /** id -> jack element registry, kept up to date by the pedal row. */
  jacksRef: React.MutableRefObject<Map<string, JackPair>>;
  /** Anchor behind the amp head where the last pedal's cable disappears to. */
  ampAnchorRef: React.RefObject<HTMLElement | null>;
  /** True while a drag or settle-spring is in flight — recompute every frame. */
  active: boolean;
}

interface Viewport {
  width: number;
  height: number;
}

/**
 * SVG overlay: an arched patch cable from each pedal's OUT jack up and over
 * into the next pedal's IN jack, and a final orthogonal run from the last
 * pedal's OUT jack up to the anchor behind the amp. Endpoints are each
 * port's top-center, so the cable visibly enters the socket rather than
 * floating beside it. Pure geometry read live from the DOM every time
 * something could have moved — no hardcoded coordinates.
 */
export function CableLayer({ containerRef, order, jacksRef, ampAnchorRef, active }: CableLayerProps) {
  const [paths, setPaths] = useState<string[]>([]);
  const [viewport, setViewport] = useState<Viewport>({ width: 0, height: 0 });
  const rafRef = useRef<number | null>(null);

  const recompute = useCallback(() => {
    const container = containerRef.current;
    if (!container) return;
    const containerRect = container.getBoundingClientRect();
    setViewport({ width: containerRect.width, height: containerRect.height });

    const jacks = jacksRef.current;
    const next: string[] = [];

    for (let i = 0; i < order.length - 1; i++) {
      const from = jacks?.get(order[i]);
      const to = jacks?.get(order[i + 1]);
      if (!from?.outEl || !to?.inEl) continue;
      const a = portTopCenter(from.outEl.getBoundingClientRect(), containerRect);
      const b = portTopCenter(to.inEl.getBoundingClientRect(), containerRect);
      next.push(pedalArchPath(a, b));
    }

    const lastId = order[order.length - 1];
    const last = lastId ? jacks?.get(lastId) : undefined;
    const anchor = ampAnchorRef.current;
    if (last?.outEl && anchor) {
      const a = portTopCenter(last.outEl.getBoundingClientRect(), containerRect);
      const b = portTopCenter(anchor.getBoundingClientRect(), containerRect);
      next.push(ampRoutePath(a, b));
    }

    setPaths(next);
  }, [ampAnchorRef, containerRef, jacksRef, order]);

  // Static recompute: mount, order changes, and container resize.
  useEffect(() => {
    recompute();
    const container = containerRef.current;
    if (!container || typeof ResizeObserver === "undefined") return;
    const observer = new ResizeObserver(() => recompute());
    observer.observe(container);
    return () => observer.disconnect();
  }, [containerRef, recompute]);

  // Animated recompute: every frame while a drag/settle is in flight, so
  // cables joggle along with the pedals they're attached to.
  useEffect(() => {
    if (!active) return;
    const tick = () => {
      recompute();
      rafRef.current = requestAnimationFrame(tick);
    };
    rafRef.current = requestAnimationFrame(tick);
    return () => {
      if (rafRef.current !== null) cancelAnimationFrame(rafRef.current);
      rafRef.current = null;
    };
  }, [active, recompute]);

  return (
    <svg
      className="cable-layer"
      width={viewport.width}
      height={viewport.height}
      viewBox={`0 0 ${viewport.width} ${viewport.height}`}
      aria-hidden="true"
    >
      {paths.map((d, i) => (
        <g key={i}>
          <path d={d} className="cable-line" />
          <path d={d} className="cable-highlight" />
        </g>
      ))}
    </svg>
  );
}
