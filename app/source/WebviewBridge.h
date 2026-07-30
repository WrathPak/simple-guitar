#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

/**
    Bridge between the React UI (loaded into a WebBrowserComponent) and the
    processor's APVTS params, NAM/IR loaders, managed library, and MeterTap.

    Wire contract (matches ui/src/bridge/*.ts):

    Every param id in SimpleGuitarAudioProcessor::allParamIds is its own
    channel (name == the id, e.g. "outputGain", "gateThresholdDb", ...):

      JS -> native, channel "<paramId>":
        { type: "valueChanged", value: <0..1 normalized> }
        { type: "gestureStart" }
        { type: "gestureEnd" }
      native -> JS, channel "<paramId>":
        { type: "valueChanged", value: <0..1 normalized> }
          -- pushed once right after the page finishes loading, and again
             whenever the host changes the parameter without the UI having
             caused it (automation, preset/state restore).

    native -> JS, channel "meterFrame" (~30Hz, via
      WebBrowserComponent::emitEventIfBrowserIsVisible so nothing is sent
      while the editor isn't visible):
      { inPeakDb: number, outPeakDb: number }

    JS -> native, channel "loadNamModel": { type: "loadNamModel", path: string }
    JS -> native, channel "loadIr":       { type: "loadIr", path: string }
    JS -> native, channel "requestRigState": { type: "requestRigState" }
      -- path must resolve inside the managed library folder (see
         RigLibrary.h); anything else is rejected with a loadResult{ok:false}
         and no load is attempted.

    JS -> native, channel "setSlotType": { type: "setSlotType", slot: 0..5, pedalType: 0..6 }
      -- picks a new pedal (or empty, pedalType 0) for the given slot; the
         engine resets that slot's on/P1-4 to their flat defaults
         (true/0.5/0.5/0.5/0.5), rebuilds the slot's DSP instance with a
         short click-free crossfade (see PluginProcessor::setSlotType()),
         and echoes a fresh "rigState". slot/pedalType out of range is
         silently rejected -- no state change, no reply.

    JS -> native, channel "movePedal": { type: "movePedal", from: 0..5, to: 0..5 }
      -- moves the pedal at slot `from` to slot `to`; every slot strictly
         between them shifts by one to close the gap (a standard array
         move over all 6 slots, including empty ones). Only the affected
         slots' PARAMETER VALUES (type/on/P1-4) move, via
         setValueNotifyingHost -- host automation lanes stay slot-
         positional; nothing about the underlying DSP instances is touched
         directly by this message (see PluginProcessor::movePedal()).
         from/to out of range is silently rejected; from == to is a
         harmless no-op that still echoes "rigState". Success (including
         the no-op case) echoes a fresh "rigState".

    Hosted third-party plugins (M3, see PluginProcessor.h's class-level
    comment and PluginHosting.h):

    JS -> native, channel "requestPluginScan": { type: "requestPluginScan" }
      -- starts an out-of-process, incremental scan of the standard VST3
         locations (+ user-added folders from the persisted cache). Replies
         with "pluginScanProgress" events while scanning and a single
         "pluginScanDone" + fresh "rigState" when finished. Ignored while a
         scan is already running; if scanning is unavailable (hosted/
         non-standalone build), replies immediately with a
         pluginScanDone over the existing cache.

    JS -> native, channel "setSlotPlugin": { type: "setSlotPlugin", slot: 0..5, pluginId: string }
      -- puts the scanned plugin (a rigState.plugins[].id) into the slot:
         type -> 7, on/P1-4 reset to flat defaults (P1 is the engine-side
         wet/dry mix), async instance build starts. Echoes a fresh
         "rigState" immediately; a "loadResult" kind:"plugin" + another
         "rigState" follow when the build resolves. slot/pluginId invalid
         -> silently rejected.

    JS -> native, channel "openPluginEditor": { type: "openPluginEditor", slot: 0..5 }
      -- opens/refocuses the hosted plugin's own editor in a floating
         native window (the sanctioned native-window exception). Silently
         ignored unless the slot has a live hosted instance.

    native -> JS, channel "pluginScanProgress":
      { type: "pluginScanProgress", done, total, currentName }
    native -> JS, channel "pluginScanDone":
      { type: "pluginScanDone", found, failed: [names] }
      -- failed lists candidates that crashed/failed this run (they're
         blacklisted and skipped on future scans). Always followed by a
         fresh "rigState".

    native -> JS, channel "loadResult":
      { type: "loadResult", kind: "nam" | "ir" | "plugin", ok: boolean, message: string }
      -- sent once a loadNamModel/loadIr request (valid or rejected) has been
         resolved, or when a hosted-plugin instance build resolves (kind
         "plugin" -- e.g. rejecting a plugin that can't do stereo in/out);
         always followed by a fresh "rigState".

    native -> JS, channel "rigState":
      { type: "rigState", schemaVersion, namModelName: string|null,
        namModelSampleRate: number, irName: string|null,
        slots: [{type: 0..7, on: boolean, pluginName?: string|null,
                 pluginMissing?: boolean}, ...6 entries],
        library: { models: [{name,path}], irs: [{name,path}] },
        plugins: [{id,name,vendor}] }
      -- sent on page load, after any load request, setSlotType, movePedal,
         or setSlotPlugin resolves, after pluginScanDone, and in reply to
         "requestRigState". slots[i] is slot i's current pedal type + on/off
         -- slot index IS signal order, there is no separate chain-order
         concept; type-7 entries additionally carry pluginName (null until
         a plugin is picked) and pluginMissing (persisted plugin not
         installed -- slot processes as bypass, revives on rescan).
         plugins is the scanned list, sorted by name. Rescans the library
         folders each time (the plugins list comes from the in-memory
         cache -- only requestPluginScan rescans plugins).

    Presets (see app/source/PresetStore.h for the on-disk format and
    PluginProcessor.h for the manager itself):

    JS -> native, channel "loadPreset":         { type: "loadPreset", path: string }
    JS -> native, channel "savePresetAs":       { type: "savePresetAs", name: string }
    JS -> native, channel "saveCurrentPreset":  { type: "saveCurrentPreset" }
    JS -> native, channel "nextPreset":         { type: "nextPreset" }
    JS -> native, channel "prevPreset":         { type: "prevPreset" }
    JS -> native, channel "requestPresets":     { type: "requestPresets" }

    native -> JS, channel "presetResult":
      { type: "presetResult", ok: boolean, message: string }
      -- sent once a loadPreset/savePresetAs/saveCurrentPreset request (valid
         or rejected) has been resolved; always followed by a fresh
         "presetsState". Not sent for nextPreset/prevPreset/requestPresets
         (no inline failure surface for those in the UI).

    native -> JS, channel "presetsState":
      { type: "presetsState", schemaVersion, current: {name,path}|null,
        dirty: boolean, presets: [{name,path}] }
      -- sent on page load, after any preset op resolves, in reply to
         "requestPresets", and whenever `dirty` flips (throttled to ~4/s max
         so a fast knob drag doesn't flood the bridge). Rescans the Presets
         folder each time.

    See schema/bridge.schema.json for the authoritative message shapes.
*/
class WebviewBridge final : private juce::Timer
{
public:
    explicit WebviewBridge (SimpleGuitarAudioProcessor& processorToUse);
    ~WebviewBridge() override;

    static constexpr const char* meterFrameChannelId = "meterFrame";
    static constexpr const char* loadNamModelChannelId = "loadNamModel";
    static constexpr const char* loadIrChannelId = "loadIr";
    static constexpr const char* requestRigStateChannelId = "requestRigState";
    static constexpr const char* setSlotTypeChannelId = "setSlotType";
    static constexpr const char* movePedalChannelId = "movePedal";
    static constexpr const char* requestPluginScanChannelId = "requestPluginScan";
    static constexpr const char* setSlotPluginChannelId = "setSlotPlugin";
    static constexpr const char* openPluginEditorChannelId = "openPluginEditor";
    static constexpr const char* pluginScanProgressChannelId = "pluginScanProgress";
    static constexpr const char* pluginScanDoneChannelId = "pluginScanDone";
    static constexpr const char* rigStateChannelId = "rigState";
    static constexpr const char* loadResultChannelId = "loadResult";
    static constexpr const char* loadPresetChannelId = "loadPreset";
    static constexpr const char* savePresetAsChannelId = "savePresetAs";
    static constexpr const char* saveCurrentPresetChannelId = "saveCurrentPreset";
    static constexpr const char* nextPresetChannelId = "nextPreset";
    static constexpr const char* prevPresetChannelId = "prevPreset";
    static constexpr const char* requestPresetsChannelId = "requestPresets";
    static constexpr const char* presetsStateChannelId = "presetsState";
    static constexpr const char* presetResultChannelId = "presetResult";

    /** Bridge protocol version, carried on every rigState/presetsState
        message (see schema/bridge.schema.json SchemaVersion). v5 adds
        hosted third-party plugins (pedal type 7, requestPluginScan/
        setSlotPlugin/openPluginEditor, pluginScanProgress/pluginScanDone,
        rigState.plugins + per-slot pluginName/pluginMissing). */
    static constexpr int schemaVersion = 5;

    /** Registers the valueChanged/gestureStart/gestureEnd listener for every
        param channel plus the loadNamModel/loadIr/requestRigState command
        channels, and returns the updated Options (builder pattern) -- chain
        this while constructing the WebBrowserComponent::Options. */
    juce::WebBrowserComponent::Options attachListenersTo (juce::WebBrowserComponent::Options opts);

    /** Call once the WebBrowserComponent exists (e.g. from pageFinishedLoading()).
        Pushes the current value of every param plus a fresh rigState to the UI,
        and starts the ~30Hz meter/automation-echo timer. Safe to call more than
        once (e.g. on page reload); each call re-pushes everything. */
    void attachBrowser (juce::WebBrowserComponent* browserToUse);

private:
    void handleParamEvent (int paramIndex, const juce::var& event);
    void handleLoadNamModel (const juce::var& event);
    void handleLoadIr (const juce::var& event);
    void handleRequestRigState (const juce::var& event);
    void handleSetSlotType (const juce::var& event);
    void handleMovePedal (const juce::var& event);
    void handleRequestPluginScan (const juce::var& event);
    void handleSetSlotPlugin (const juce::var& event);
    void handleOpenPluginEditor (const juce::var& event);
    void handleLoadPreset (const juce::var& event);
    void handleSavePresetAs (const juce::var& event);
    void handleSaveCurrentPreset (const juce::var& event);
    void handleNextPreset (const juce::var& event);
    void handlePrevPreset (const juce::var& event);
    void handleRequestPresets (const juce::var& event);

    void pushParamValueToUi (int paramIndex);
    void sendRigState();
    void sendLoadResult (const char* kind, bool ok, const juce::String& message);
    void sendPluginScanProgress (int done, int total, const juce::String& currentName);
    void sendPluginScanDone (int found, const juce::StringArray& failedNames);
    void sendPresetsState();
    void sendPresetResult (bool ok, const juce::String& message);

    void timerCallback() override;

    SimpleGuitarAudioProcessor& processor;
    juce::WebBrowserComponent* browser = nullptr;

    // One entry per SimpleGuitarAudioProcessor::allParamIds, same order.
    std::array<juce::RangedAudioParameter*, SimpleGuitarAudioProcessor::numParams> params {};

    // Last normalized value we know the UI has for each param (either
    // because we just sent it, or the UI just told us). Used to suppress
    // redundant automation-echo events and avoid a feedback loop.
    std::array<std::atomic<float>, SimpleGuitarAudioProcessor::numParams> lastKnownUiValue;

    // Dirty-flip throttle for presetsState (see class comment on the
    // "presetsState" channel above): sendPresetsState() itself refreshes
    // both of these on every call, whatever the caller.
    bool lastKnownPresetDirty = false;
    juce::uint32 lastPresetDirtyEmitMs = 0;
    static constexpr juce::uint32 presetDirtyEmitMinIntervalMs = 250; // ~4/s max

    static constexpr float meterFloorDb = -60.0f;
    static constexpr int meterTimerHz = 30;

    // NAM model loads complete asynchronously on the message thread (via
    // MessageManager::callAsync inside sg::NamProcessor); if the editor (and
    // this bridge) is destroyed while a load is in flight, the completion
    // lambda must not touch a dangling `this`. IR loads complete
    // synchronously today but are guarded the same way defensively.
    JUCE_DECLARE_WEAK_REFERENCEABLE (WebviewBridge)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebviewBridge)
};
