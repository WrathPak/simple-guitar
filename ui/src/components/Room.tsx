import { useEffect, useRef, useState } from "react";
import {
  sendLoadIr,
  sendLoadNamModel,
  sendRequestRigState,
  subscribeLoadResult,
  subscribeMeterFrame,
  subscribeRigState,
  useParam,
  type LoadResult,
  type MeterFrame,
  type RigState,
} from "../bridge";
import { AmpDevice } from "./AmpDevice";
import { Backline } from "./Backline";
import { CableLayer, type JackPair } from "./CableLayer";
import { CabDevice } from "./CabDevice";
import { Chrome } from "./Chrome";
import { GateOverlay } from "./GateOverlay";
import { LibraryOverlay } from "./LibraryOverlay";
import { METER_MIN_DB, nextPeakHold } from "./meterMath";
import {
  AMP_DB_DEFAULT_NORMALIZED,
  CAB_HIGH_CUT_RANGE,
  CAB_LOW_CUT_RANGE,
  GATE_THRESHOLD_DEFAULT_NORMALIZED,
  defaultNormalizedForLogHz,
  isParamOn,
} from "./paramMath";
import { DEMO_PEDALS } from "./pedalDefs";
import { PedalDevice } from "./PedalDevice";
import { PedalRow, type PedalRowState } from "./PedalRow";
import "./Room.css";

const OUTPUT_DEFAULT_VALUE = 0.75;
const CAB_LOW_CUT_DEFAULT = defaultNormalizedForLogHz(CAB_LOW_CUT_RANGE);
const CAB_HIGH_CUT_DEFAULT = defaultNormalizedForLogHz(CAB_HIGH_CUT_RANGE);

const EMPTY_RIG_STATE: RigState = {
  type: "rigState",
  schemaVersion: 1,
  namModelName: null,
  namModelSampleRate: 0,
  irName: null,
  library: { models: [], irs: [] },
};

const WIDE_HINT = "Click gear to focus · drag pedals to reorder · footswitch = bypass";
const FOCUSED_HINT = "Click outside or press Esc to step back";
const DELAY_CONTEXTUAL = "Tap · 122 BPM · Sync ¼";

const MODEL_LIBRARY_EMPTY_COPY = "no models found — drop .nam files in Documents\\Simple Guitar\\Models";
const IR_LIBRARY_EMPTY_COPY = "no irs found — drop .wav/.aiff files in Documents\\Simple Guitar\\IRs";

/** Camera target: null = wide shot, "amp"/"cab" = the backline gear, else a pedal id. */
type FocusTarget = "amp" | "cab" | string | null;

/** Which full-window overlay (if any) is open. Independent of camera focus. */
type OverlayKind = "model" | "ir" | "gate" | null;

/** The whole cinematic scene: one room, the rig in it, and the chrome around it. */
export function Room() {
  // All 12 host params are subscribed here, up front, rather than lazily
  // from whichever device/overlay ends up using them -- see useParam's docs
  // for why that timing matters.
  const outputGain = useParam("outputGain", OUTPUT_DEFAULT_VALUE);
  const ampInputDb = useParam("ampInputDb", AMP_DB_DEFAULT_NORMALIZED);
  const ampBassDb = useParam("ampBassDb", AMP_DB_DEFAULT_NORMALIZED);
  const ampMidDb = useParam("ampMidDb", AMP_DB_DEFAULT_NORMALIZED);
  const ampTrebleDb = useParam("ampTrebleDb", AMP_DB_DEFAULT_NORMALIZED);
  const ampPresenceDb = useParam("ampPresenceDb", AMP_DB_DEFAULT_NORMALIZED);
  const namNormalize = useParam("namNormalize", 0);
  const cabOn = useParam("cabOn", 1);
  const cabLowCutHz = useParam("cabLowCutHz", CAB_LOW_CUT_DEFAULT);
  const cabHighCutHz = useParam("cabHighCutHz", CAB_HIGH_CUT_DEFAULT);
  const gateOn = useParam("gateOn", 0);
  const gateThresholdDb = useParam("gateThresholdDb", GATE_THRESHOLD_DEFAULT_NORMALIZED);

  const [rigState, setRigState] = useState<RigState>(EMPTY_RIG_STATE);
  const [loadResult, setLoadResult] = useState<LoadResult | null>(null);
  const [overlayKind, setOverlayKind] = useState<OverlayKind>(null);

  const [inMeter, setInMeter] = useState({ peakDb: METER_MIN_DB, holdDb: METER_MIN_DB });
  const [outMeter, setOutMeter] = useState({ peakDb: METER_MIN_DB, holdDb: METER_MIN_DB });

  const [pedals, setPedals] = useState<PedalRowState[]>(() =>
    DEMO_PEDALS.map((pedal) => ({ ...pedal, bypassed: pedal.defaultBypassed })),
  );
  const [focus, setFocus] = useState<FocusTarget>(null);

  // Cable geometry: read live off the DOM, never hardcoded. `jacksRef` and
  // `ampAnchorRef` are populated by the pedal row / backline as they mount;
  // `sceneRef` is the coordinate space cable points are measured relative to.
  const sceneRef = useRef<HTMLDivElement>(null);
  const jacksRef = useRef(new Map<string, JackPair>());
  const ampAnchorRef = useRef<HTMLElement | null>(null);
  const [cablesActive, setCablesActive] = useState(false);

  useEffect(() => subscribeRigState(setRigState), []);
  useEffect(() => subscribeLoadResult(setLoadResult), []);

  useEffect(() => {
    return subscribeMeterFrame((frame: MeterFrame) => {
      setInMeter((prev) => ({ peakDb: frame.inPeakDb, holdDb: nextPeakHold(prev.holdDb, frame.inPeakDb) }));
      setOutMeter((prev) => ({ peakDb: frame.outPeakDb, holdDb: nextPeakHold(prev.holdDb, frame.outPeakDb) }));
    });
  }, []);

  useEffect(() => {
    if (focus === null) return;
    function onKeyDown(e: KeyboardEvent) {
      // While an overlay is open, Esc closes it first (Overlay owns that);
      // a second Esc (once the overlay's gone) steps back out of focus.
      if (e.key === "Escape" && overlayKind === null) setFocus(null);
    }
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [focus, overlayKind]);

  const toggleBypass = (id: string) => {
    setPedals((prev) => prev.map((p) => (p.id === id ? { ...p, bypassed: !p.bypassed } : p)));
  };

  /**
   * Commits a new left-to-right chain order (drag drop or keyboard swap).
   * The order lives here as a plain list of ids — the one place it's
   * authoritative — so the pedal row and the cable layer both just derive
   * their layout from it. Audio is a fixed passthrough for now; once the
   * real chain lands this is the one spot that would also send an
   * order-changed message across the bridge.
   */
  const reorderPedals = (order: string[]) => {
    setPedals((prev) => {
      const byId = new Map(prev.map((p) => [p.id, p]));
      const next = order.map((id) => byId.get(id)).filter((p): p is PedalRowState => p !== undefined);
      return next.length === prev.length ? next : prev;
    });
  };

  const closeOverlay = () => setOverlayKind(null);
  const isCabOn = isParamOn(cabOn.value);
  const isNormalizeOn = isParamOn(namNormalize.value);

  const focusedPedal = typeof focus === "string" && focus !== "amp" && focus !== "cab" ? pedals.find((p) => p.id === focus) : undefined;
  const hint = focus === null ? WIDE_HINT : FOCUSED_HINT;
  const contextual = focusedPedal?.family === "delay" ? DELAY_CONTEXTUAL : undefined;

  return (
    <div className="room">
      <div className="room-scene-wrap" onClick={focus !== null ? () => setFocus(null) : undefined}>
        <div ref={sceneRef} className={`room-scene${focus !== null ? " room-scene--blurred" : ""}`}>
          <div className="room-floor" />
          <CableLayer
            containerRef={sceneRef}
            order={pedals.map((p) => p.id)}
            jacksRef={jacksRef}
            ampAnchorRef={ampAnchorRef}
            active={cablesActive}
          />
          <Backline
            onFocusAmp={() => setFocus("amp")}
            onFocusCab={() => setFocus("cab")}
            ampAnchorRef={ampAnchorRef}
            namModelName={rigState.namModelName}
            cabOn={isCabOn}
          />
          <PedalRow
            pedals={pedals}
            onFocusPedal={setFocus}
            onToggleBypass={toggleBypass}
            onReorder={reorderPedals}
            jacksRef={jacksRef}
            onActivityChange={setCablesActive}
          />
        </div>
      </div>

      <div className="room-focus-wrap">
        {focus === "amp" && (
          <AmpDevice
            focused
            namModelName={rigState.namModelName}
            normalizeOn={isNormalizeOn}
            onToggleNormalize={() => namNormalize.setValue(isNormalizeOn ? 0 : 1)}
            onOpenModelOverlay={() => setOverlayKind("model")}
            knobs={{ input: ampInputDb, bass: ampBassDb, mid: ampMidDb, treble: ampTrebleDb, presence: ampPresenceDb, output: outputGain }}
          />
        )}
        {focus === "cab" && (
          <CabDevice
            focused
            irName={rigState.irName}
            cabOn={isCabOn}
            onToggleCabOn={() => cabOn.setValue(isCabOn ? 0 : 1)}
            onOpenIrOverlay={() => setOverlayKind("ir")}
            lowCut={cabLowCutHz}
            highCut={cabHighCutHz}
          />
        )}
        {focusedPedal && (
          <PedalDevice
            pedal={focusedPedal}
            bypassed={focusedPedal.bypassed}
            focused
            onToggleBypass={() => toggleBypass(focusedPedal.id)}
          />
        )}
      </div>

      {overlayKind === "model" && (
        <LibraryOverlay
          kind="nam"
          title="Change model"
          emptyCopy={MODEL_LIBRARY_EMPTY_COPY}
          library={rigState.library.models}
          loadResult={loadResult}
          onSelect={sendLoadNamModel}
          onClose={closeOverlay}
          onOpen={sendRequestRigState}
        />
      )}
      {overlayKind === "ir" && (
        <LibraryOverlay
          kind="ir"
          title="Change IR"
          emptyCopy={IR_LIBRARY_EMPTY_COPY}
          library={rigState.library.irs}
          loadResult={loadResult}
          onSelect={sendLoadIr}
          onClose={closeOverlay}
          onOpen={sendRequestRigState}
        />
      )}
      {overlayKind === "gate" && <GateOverlay gateOn={gateOn} gateThreshold={gateThresholdDb} onClose={closeOverlay} />}

      <Chrome
        presetName="Blues Breakup 02"
        unsaved
        inputMeter={inMeter}
        outputMeter={outMeter}
        hint={hint}
        contextual={contextual}
        onSelectGate={() => setOverlayKind("gate")}
      />
    </div>
  );
}
