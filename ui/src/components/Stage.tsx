import type { ReactNode } from "react";
import "./Stage.css";

export interface StageProps {
  children?: ReactNode;
}

/** Cinematic stage: radial light + bottom vignette, one device centered. */
export function Stage({ children }: StageProps) {
  return (
    <div className="stage">
      <div className="stage-content">{children}</div>
    </div>
  );
}
