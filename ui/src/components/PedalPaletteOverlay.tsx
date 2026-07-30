import { useEffect, useState } from "react";
import type { LoadResult, PluginEntry } from "../bridge";
import "./PedalPaletteOverlay.css";
import { Overlay } from "./Overlay";
import { PEDAL_TYPES } from "./pedalDefs";

export interface PedalPaletteOverlayProps {
  /** Called with the picked slot{N}Type value (1..6). The caller sends setSlotType and closes. */
  onPick: (pedalType: number) => void;
  /** Scanned third-party plugins, sorted by name (rigState.plugins). */
  plugins: PluginEntry[];
  /** Called with a plugins[].id. The caller sends setSlotType(slot, 7) then setSlotPlugin(slot, id). */
  onPickPlugin: (pluginId: string) => void;
  /** Kicks off a plugin scan (requestPluginScan). */
  onScan: () => void;
  /** True while a scan is running -- the scan action reports itself instead of firing again. */
  scanning: boolean;
  /** Most recent loadResult from the bridge, of any kind -- only kind "plugin" is reacted to. */
  loadResult: LoadResult | null;
  onClose: () => void;
}

const PLUGINS_EMPTY_COPY = "no plugins yet";

/**
 * The add-pedal palette: an overlay over the darkened room -- never a
 * persistent panel -- listing the six built-in pedal types as small
 * pedal-shaped tiles in their family colors, and below them the third-party
 * plugins the engine has scanned, as quiet rows. Click either to drop it
 * into the first empty slot.
 *
 * A built-in type is placed instantly. A plugin has to be built by the
 * engine first, so its row waits on the loadResult (same pattern as the
 * model/IR pickers): the overlay stays open until the build resolves, then
 * closes on success or reports the failure right here, where the pick was
 * made.
 */
export function PedalPaletteOverlay({ onPick, plugins, onPickPlugin, onScan, scanning, loadResult, onClose }: PedalPaletteOverlayProps) {
  const [pendingPluginId, setPendingPluginId] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (pendingPluginId === null || loadResult === null || loadResult.kind !== "plugin") return;
    if (loadResult.ok) {
      onClose();
    } else {
      setPendingPluginId(null);
      setError(loadResult.message);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [loadResult]);

  const handlePickPlugin = (plugin: PluginEntry) => {
    setError(null);
    setPendingPluginId(plugin.id);
    onPickPlugin(plugin.id);
  };

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

      <div className="pedal-palette-plugins">
        <div className="pedal-palette-section">your plugins</div>
        {plugins.length === 0 ? (
          <div className="pedal-palette-plugins-empty">
            <span>{PLUGINS_EMPTY_COPY}</span>
            <button type="button" className="pedal-palette-scan" disabled={scanning} onClick={onScan}>
              {scanning ? "scanning" : "scan for plugins"}
            </button>
          </div>
        ) : (
          <div className="pedal-palette-plugin-list">
            {plugins.map((plugin) => (
              <button
                key={plugin.id}
                type="button"
                className="pedal-palette-plugin-row"
                disabled={pendingPluginId !== null}
                onClick={() => handlePickPlugin(plugin)}
              >
                <span className="pedal-palette-plugin-name">{plugin.name}</span>
                {pendingPluginId === plugin.id && <span className="pedal-palette-plugin-status">loading</span>}
              </button>
            ))}
          </div>
        )}
      </div>

      {error && <div className="pedal-palette-error">{error}</div>}
    </Overlay>
  );
}
