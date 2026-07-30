/**
 * Monoline chain-rail glyphs (1.4 stroke), copied verbatim from
 * the Direction D reference render (the visual reference of record) so the
 * rail matches the locked mockup exactly. `currentColor` drives on/off/staged
 * styling from CSS — see ChainRail.css.
 */
import type { SVGProps } from "react";

export type IconProps = SVGProps<SVGSVGElement>;

export function GateIcon(props: IconProps) {
  return (
    <svg width="16" height="12" viewBox="0 0 16 12" fill="none" stroke="currentColor" strokeWidth="1.4" {...props}>
      <path d="M1 11 V4 M5 11 V1 M10.5 11 V6 M15 11 V3" />
    </svg>
  );
}

export function CompIcon(props: IconProps) {
  return (
    <svg width="16" height="12" viewBox="0 0 16 12" fill="none" stroke="currentColor" strokeWidth="1.4" {...props}>
      <path d="M1 9 C5 9 5 3 8 3 C11 3 11 6 15 6" />
    </svg>
  );
}

export function DriveIcon(props: IconProps) {
  return (
    <svg width="16" height="12" viewBox="0 0 16 12" fill="none" stroke="currentColor" strokeWidth="1.4" {...props}>
      <path d="M1 6 h3 l2 -4 4 8 2 -4 h3" />
    </svg>
  );
}

export function AmpIcon(props: IconProps) {
  return (
    <svg width="18" height="13" viewBox="0 0 18 13" fill="none" stroke="currentColor" strokeWidth="1.4" {...props}>
      <rect x="1" y="1" width="16" height="11" rx="2" />
      <path d="M4 9 h.01 M8 9 h.01 M12 9 h.01" />
      <path d="M3 5.5 h12" opacity=".6" />
    </svg>
  );
}

export function CabIcon(props: IconProps) {
  return (
    <svg width="15" height="13" viewBox="0 0 15 13" fill="none" stroke="currentColor" strokeWidth="1.4" {...props}>
      <rect x="1" y="1" width="13" height="11" rx="2" />
      <circle cx="7.5" cy="6.5" r="3.2" />
    </svg>
  );
}

export function DelayIcon(props: IconProps) {
  return (
    <svg width="16" height="12" viewBox="0 0 16 12" fill="none" stroke="currentColor" strokeWidth="1.4" {...props}>
      <path d="M2 2 v8 M7 4 v6 M12 6 v4" />
    </svg>
  );
}

export function ReverbIcon(props: IconProps) {
  return (
    <svg width="16" height="12" viewBox="0 0 16 12" fill="none" stroke="currentColor" strokeWidth="1.4" {...props}>
      <path d="M8 1 a5 5 0 0 1 0 10 M8 3.5 a2.5 2.5 0 0 1 0 5" opacity=".9" />
      <path d="M8 1 a5 5 0 0 0 0 10" opacity=".4" />
    </svg>
  );
}
