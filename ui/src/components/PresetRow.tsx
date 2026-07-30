import "./PresetRow.css";

/**
 * `‹ Name ● › SAVE SAVE-AS`. Static placeholder for M0 —
 * name-menu, unsaved tracking, and save actions are not wired yet.
 */
export function PresetRow() {
  return (
    <div className="preset-row">
      <span className="preset-row-nav" aria-hidden="true">
        ‹
      </span>
      <span className="preset-row-name">
        Blues Breakup 02 <span className="preset-row-unsaved">●</span>
      </span>
      <span className="preset-row-nav" aria-hidden="true">
        ›
      </span>
      <span className="preset-row-action">Save</span>
      <span className="preset-row-action">Save as</span>
    </div>
  );
}
