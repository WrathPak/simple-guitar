import "./BotBar.css";

/**
 * Bottom utility bar per DESIGN.md §1: Tap/BPM/Metronome | credit |
 * Presets/Library/Settings. Static placeholder for M0.
 */
export function BotBar() {
  return (
    <div className="botbar">
      <div className="botbar-group">
        <span className="botbar-label botbar-label--active">Tap</span>
        <span className="botbar-label">120 BPM</span>
        <span className="botbar-label">Metronome</span>
      </div>
      <div className="botbar-group">
        <span className="botbar-label">Powered by NAM</span>
      </div>
      <div className="botbar-group">
        <span className="botbar-label">Presets</span>
        <span className="botbar-label">Library</span>
        <span className="botbar-label botbar-label--active">Settings</span>
      </div>
    </div>
  );
}
