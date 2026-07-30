import { useEffect, useRef, useState } from "react";
import {
  sendLoadIr,
  sendLoadNamModel,
  sendLoadPreset,
  sendMovePedal,
  sendNextPreset,
  sendPrevPreset,
  sendRequestPresets,
  sendRequestRigState,
  sendSaveCurrentPreset,
  sendSavePresetAs,
  sendSetSlotType,
  subscribeLoadResult,
  subscribeMeterFrame,
  subscribePresetResult,
  subscribePresetsState,
  subscribeRigState,
  useParam,
  type LoadResult,
  type MeterFrame,
  type ParamHandle,
  type PresetResult,
  type PresetsState,
  type RigState,
  type SlotState,
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
import { EMPTY_SLOT_TYPE, pedalTypeDef, slotDomId, slotParamId } from "./pedalDefs";
import { PedalDevice } from "./PedalDevice";
import { PedalPaletteOverlay } from "./PedalPaletteOverlay";
import { PedalRow, type FloorPedal } from "./PedalRow";
import { PresetsOverlay } from "./PresetsOverlay";
import "./Room.css";
import { SaveAsOverlay } from "./SaveAsOverlay";

const OUTPUT_DEFAULT_VALUE = 0.75;
const CAB_LOW_CUT_DEFAULT = defaultNormalizedForLogHz(CAB_LOW_CUT_RANGE);
const CAB_HIGH_CUT_DEFAULT = defaultNormalizedForLogHz(CAB_HIGH_CUT_RANGE);

/** The default floor (the classic trio in slots 0-2) -- matches the engine's
    own default slot state, and the dev fallback's (see bridge/rig.ts). The
    slot params default flat: every slot{N}On true, every P 0.5, regardless
    of type. */
const DEFAULT_SLOTS: RigState["slots"] = [
  { type: 1, on: true },
  { type: 2, on: true },
  { type: 3, on: true },
  { type: 0, on: true },
  { type: 0, on: true },
  { type: 0, on: true },
];

const EMPTY_RIG_STATE: RigState = {
  type: "rigState",
  schemaVersion: 4,
  namModelName: null,
  namModelSampleRate: 0,
  irName: null,
  slots: DEFAULT_SLOTS,
  library: { models: [], irs: [] },
};

const EMPTY_PRESETS_STATE: PresetsState = {
  type: "presetsState",
  schemaVersion: 4,
  current: null,
  dirty: false,
  presets: [],
};

const WIDE_HINT = "Click gear to focus · drag pedals to reorder · footswitch = bypass";
const FOCUSED_HINT = "Click outside or press Esc to step back";

const MODEL_LIBRARY_EMPTY_COPY = "no models found — drop .nam files in Documents\\Simple Guitar\\Models";
const IR_LIBRARY_EMPTY_COPY = "no irs found — drop .wav/.aiff files in Documents\\Simple Guitar\\IRs";

/** Camera target: null = wide shot, "amp"/"cab" = the backline gear, else an occupied slot index. */
type FocusTarget = "amp" | "cab" | number | null;

/** Which full-window overlay (if any) is open. Independent of camera focus. */
type OverlayKind = "model" | "ir" | "gate" | "presets" | "saveAs" | "palette" | null;

/** One slot's live param handles: footswitch (On) + the four generic knobs (P1..P4). */
interface SlotParams {
  on: ParamHandle;
  knobs: [ParamHandle, ParamHandle, ParamHandle, ParamHandle];
}

/**
 * Subscribes one slot's 5 live params (slot{N}On + slot{N}P1..P4). Same
 * subscribe-up-front convention as every other param in Room -- the engine
 * pushes each param's initial value exactly once, right after page load, so
 * these can't be subscribed lazily when a slot happens to fill.
 */
function useSlotParams(slot: number): SlotParams {
  const on = useParam(slotParamId(slot, "On"), 1);
  const p1 = useParam(slotParamId(slot, "P1"), 0.5);
  const p2 = useParam(slotParamId(slot, "P2"), 0.5);
  const p3 = useParam(slotParamId(slot, "P3"), 0.5);
  const p4 = useParam(slotParamId(slot, "P4"), 0.5);
  return { on, knobs: [p1, p2, p3, p4] };
}

/** The whole cinematic scene: one room, the rig in it, and the chrome around it. */
export function Room() {
  // All host params are subscribed here, up front, rather than lazily from
  // whichever device/overlay ends up using them -- see useParam's docs for
  // why that timing matters.
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

  // The six floor slots' 30 params (on/off + 4 generic knobs each). Which
  // pedal type (if any) occupies each slot comes from rigState.slots; the
  // registry (pedalDefs.ts) says what the knobs mean for that type.
  const slotParams: SlotParams[] = [
    useSlotParams(0),
    useSlotParams(1),
    useSlotParams(2),
    useSlotParams(3),
    useSlotParams(4),
    useSlotParams(5),
  ];

  const [rigState, setRigState] = useState<RigState>(EMPTY_RIG_STATE);
  const [loadResult, setLoadResult] = useState<LoadResult | null>(null);
  const [presetsState, setPresetsState] = useState<PresetsState>(EMPTY_PRESETS_STATE);
  const [presetResult, setPresetResult] = useState<PresetResult | null>(null);
  const [overlayKind, setOverlayKind] = useState<OverlayKind>(null);

  const [inMeter, setInMeter] = useState({ peakDb: METER_MIN_DB, holdDb: METER_MIN_DB });
  const [outMeter, setOutMeter] = useState({ peakDb: METER_MIN_DB, holdDb: METER_MIN_DB });

  // The floor's contents come straight from the engine (rigState.slots) --
  // there is no local-only slot state, so a DAW session restore or preset
  // load re-populates the floor for free, and a palette pick / remove /
  // reorder only ever *asks* the engine (setSlotType / movePedal below);
  // the floor updates once that round-trips back via the next rigState.
  // Empty slots collapse: only occupied slots reach the row, still in slot
  // (= signal) order.
  const slots: readonly SlotState[] = rigState.slots;
  const pedals: FloorPedal[] = slots
    .map((slot, index) => {
      const def = pedalTypeDef(slot.type);
      if (!def) return null;
      return { slot: index, def, bypassed: !isParamOn(slotParams[index].on.value) };
    })
    .filter((pedal): pedal is FloorPedal => pedal !== null);

  const firstEmptySlot = slots.findIndex((slot) => slot.type === EMPTY_SLOT_TYPE);

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
  useEffect(() => subscribePresetsState(setPresetsState), []);
  useEffect(() => subscribePresetResult(setPresetResult), []);

  useEffect(() => {
    return subscribeMeterFrame((frame: MeterFrame) => {
      setInMeter((prev) => ({ peakDb: frame.inPeakDb, holdDb: nextPeakHold(prev.holdDb, frame.inPeakDb) }));
      setOutMeter((prev) => ({ peakDb: frame.outPeakDb, holdDb: nextPeakHold(prev.holdDb, frame.outPeakDb) }));
    });
  }, []);

  // The one global keyboard shortcut in the app -- ctrl/cmd+S saves. Saves
  // over the current preset if there is one; opens the save-as overlay
  // otherwise.
  useEffect(() => {
    function onKeyDown(e: KeyboardEvent) {
      if (!(e.ctrlKey || e.metaKey) || e.key.toLowerCase() !== "s") return;
      e.preventDefault();
      if (presetsState.current !== null) sendSaveCurrentPreset();
      else setOverlayKind("saveAs");
    }
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [presetsState]);

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

  const toggleBypass = (slot: number) => {
    const on = slotParams[slot]?.on;
    if (!on) return;
    on.setValue(isParamOn(on.value) ? 0 : 1);
  };

  /** Palette pick: drop the chosen type into the first empty slot. The floor updates once the engine echoes the new slots on the next rigState. */
  const addPedal = (pedalType: number) => {
    if (firstEmptySlot !== -1) sendSetSlotType(firstEmptySlot, pedalType);
    setOverlayKind(null);
  };

  /** The focused pedal's quiet remove action: empty its slot, step the camera back out; the row reflows and cables re-route on the echoed rigState. */
  const removePedal = (slot: number) => {
    sendSetSlotType(slot, EMPTY_SLOT_TYPE);
    setFocus(null);
  };

  const closeOverlay = () => setOverlayKind(null);
  const isCabOn = isParamOn(cabOn.value);
  const isNormalizeOn = isParamOn(namNormalize.value);

  const focusedPedal = typeof focus === "number" ? pedals.find((p) => p.slot === focus) : undefined;
  const hint = focus === null ? WIDE_HINT : FOCUSED_HINT;
  // Chrome's contextual line (design §2) is reserved for a real time-based
  // feature (tap tempo / sync) once one exists -- no pedal type has that
  // yet, so nothing is ever passed here today.

  return (
    <div className="room">
      <div className="room-scene-wrap" onClick={focus !== null ? () => setFocus(null) : undefined}>
        <div
          ref={sceneRef}
          className={`room-scene${focus !== null ? " room-scene--blurred" : ""}`}
          // Blurred + dimmed already reads as "not here" visually; `inert`
          // makes that true for keyboard/AT too, so Tab goes straight to the
          // focused device instead of walking through dead background gear.
          inert={focus !== null ? "" : undefined}
        >
          <div className="room-floor" />
          <CableLayer
            containerRef={sceneRef}
            order={pedals.map((p) => slotDomId(p.slot))}
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
            onMovePedal={sendMovePedal}
            onAddPedal={() => setOverlayKind("palette")}
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
          <div className="room-focus-pedal">
            <PedalDevice
              pedal={focusedPedal.def}
              bypassed={focusedPedal.bypassed}
              focused
              onToggleBypass={() => toggleBypass(focusedPedal.slot)}
              knobs={focusedPedal.def.knobs.map((k) => slotParams[focusedPedal.slot].knobs[k.param - 1])}
            />
            <button type="button" className="room-focus-remove" onClick={() => removePedal(focusedPedal.slot)}>
              remove
            </button>
          </div>
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
      {overlayKind === "palette" && <PedalPaletteOverlay onPick={addPedal} onClose={closeOverlay} />}
      {overlayKind === "presets" && (
        <PresetsOverlay
          presets={presetsState.presets}
          currentPath={presetsState.current?.path ?? null}
          presetResult={presetResult}
          onSelect={sendLoadPreset}
          onClose={closeOverlay}
          onOpen={sendRequestPresets}
        />
      )}
      {overlayKind === "saveAs" && (
        <SaveAsOverlay
          initialName={presetsState.current?.name ?? ""}
          presetResult={presetResult}
          onSave={sendSavePresetAs}
          onClose={closeOverlay}
        />
      )}

      <Chrome
        presetName={presetsState.current?.name ?? null}
        dirty={presetsState.dirty}
        hasPresets={presetsState.presets.length > 0}
        onPrevPreset={sendPrevPreset}
        onNextPreset={sendNextPreset}
        onOpenPresets={() => setOverlayKind("presets")}
        onSave={() => (presetsState.current !== null ? sendSaveCurrentPreset() : setOverlayKind("saveAs"))}
        onOpenSaveAs={() => setOverlayKind("saveAs")}
        inputMeter={inMeter}
        outputMeter={outMeter}
        hint={hint}
        onSelectGate={() => setOverlayKind("gate")}
      />
    </div>
  );
}
