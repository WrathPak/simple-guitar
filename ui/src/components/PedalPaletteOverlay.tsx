import "./PedalPaletteOverlay.css";
import { Overlay } from "./Overlay";
import { PEDAL_TYPES } from "./pedalDefs";

export interface PedalPaletteOverlayProps {
  /** Called with the picked slot{N}Type value (1..6). The caller sends setSlotType and closes. */
  onPick: (pedalType: number) => void;
  onClose: () => void;
}

/**
 * The add-pedal palette: an overlay over the darkened room -- never a
 * persistent panel -- listing the six pedal types as small pedal-shaped
 * tiles in their family colors. Click one to drop it into the first empty
 * slot.
 */
export function PedalPaletteOverlay({ onPick, onClose }: PedalPaletteOverlayProps) {
  return (
    <Overlay title="add pedal" onClose={onClose}>
      <div className="pedal-palette">
        {PEDAL_TYPES.map((def) => (
          <button key={def.type} type="button" className="pedal-palette-tile" onClick={() => onPick(def.type)}>
            <span className={`pedal-palette-shell pedal--${def.family}`} aria-hidden="true">
              <span className="pedal-palette-panel" />
              <span className="pedal-palette-foot" />
            </span>
            <span className="pedal-palette-name">{def.name}</span>
          </button>
        ))}
      </div>
    </Overlay>
  );
}
