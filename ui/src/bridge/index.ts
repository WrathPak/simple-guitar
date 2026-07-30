export { isJuceHost } from "./juceClient";
export { getSliderState } from "./sliderState";
export { subscribeMeterFrame } from "./meterFrame";
export { useParam } from "./useParam";
export {
  subscribeRigState,
  subscribeLoadResult,
  subscribePluginScanProgress,
  subscribePluginScanDone,
  sendLoadNamModel,
  sendLoadIr,
  sendRequestRigState,
  sendSetSlotType,
  sendMovePedal,
  sendRequestPluginScan,
  sendSetSlotPlugin,
  sendOpenPluginEditor,
  isValidSlotIndex,
  isValidPedalType,
  PLUGIN_PEDAL_TYPE,
} from "./rig";
export {
  subscribePresetsState,
  subscribePresetResult,
  sendLoadPreset,
  sendSavePresetAs,
  sendSaveCurrentPreset,
  sendNextPreset,
  sendPrevPreset,
  sendRequestPresets,
} from "./presets";
export type { SliderStateHandle, MeterFrame } from "./types";
export type { ParamHandle } from "./useParam";
export type {
  ParamId,
  RigState,
  RigLibrary,
  LibraryEntry,
  LoadResult,
  LoadResultKind,
  SlotState,
  PluginEntry,
  PluginScanProgress,
  PluginScanDone,
  PresetsState,
  PresetResult,
} from "../../../schema/gen/ts/bridge";
