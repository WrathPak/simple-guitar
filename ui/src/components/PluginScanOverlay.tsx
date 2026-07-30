import type { PluginScanDone, PluginScanProgress } from "../bridge";
import { Overlay } from "./Overlay";
import "./PluginScanOverlay.css";

/** What Room knows about plugin scanning: whether one is running, its last progress tick, and the last summary. */
export interface PluginScanState {
  running: boolean;
  progress: PluginScanProgress | null;
  done: PluginScanDone | null;
}

export interface PluginScanOverlayProps {
  scan: PluginScanState;
  /** How many plugins the engine currently knows about (rigState.plugins). */
  pluginCount: number;
  /** Kicks off a scan (requestPluginScan). */
  onScan: () => void;
  onClose: () => void;
}

/**
 * The plugins view behind the ⋯ menu: what's installed, and the one button
 * that goes looking for more. Scanning never happens on its own -- it can
 * take a while and it runs third-party code -- so this is the only place it
 * starts from besides the palette's empty state.
 */
export function PluginScanOverlay({ scan, pluginCount, onScan, onClose }: PluginScanOverlayProps) {
  const { running, progress, done } = scan;

  return (
    <Overlay title="plugins" onClose={onClose}>
      <div className="plugin-scan">
        <div className="plugin-scan-status">
          {running ? (
            <>
              <span className="plugin-scan-count">
                {progress ? `${progress.done}/${progress.total}` : "starting"}
              </span>
              {progress && <span className="plugin-scan-current">{progress.currentName}</span>}
            </>
          ) : done ? (
            <span className="plugin-scan-count">found {done.found}</span>
          ) : (
            <span className="plugin-scan-count">{pluginCount === 0 ? "no plugins yet" : `${pluginCount} plugins`}</span>
          )}
        </div>

        <button type="button" className="plugin-scan-action" disabled={running} onClick={onScan}>
          {running ? "scanning" : "scan"}
        </button>
      </div>

      {!running && done && done.failed.length > 0 && (
        <div className="plugin-scan-failed">
          <div className="plugin-scan-failed-title">failed</div>
          {done.failed.map((name) => (
            <div key={name} className="plugin-scan-failed-row">
              {name}
            </div>
          ))}
        </div>
      )}
    </Overlay>
  );
}
