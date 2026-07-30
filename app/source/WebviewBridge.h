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

    JS -> native, channel "setChainOrder": { type: "setChainOrder", order: [pedalId, pedalId, pedalId] }
      -- order must be exactly a permutation of "screamer"/"echoes"/"chamber";
         anything else (wrong length, duplicate, unknown id) is silently
         rejected -- no state change, no loadResult, no rigState reply. On
         success the new order is applied to the audio thread immediately
         (see PluginProcessor::setChainOrder(), glitch-free/allocation-free)
         and echoed back on the next "rigState".

    native -> JS, channel "loadResult":
      { type: "loadResult", kind: "nam" | "ir", ok: boolean, message: string }
      -- sent once a loadNamModel/loadIr request (valid or rejected) has been
         resolved; always followed by a fresh "rigState".

    native -> JS, channel "rigState":
      { type: "rigState", schemaVersion, namModelName: string|null,
        namModelSampleRate: number, irName: string|null,
        chainOrder: [pedalId, pedalId, pedalId],
        library: { models: [{name,path}], irs: [{name,path}] } }
      -- sent on page load, after any load request or setChainOrder resolves,
         and in reply to "requestRigState". Rescans the library folders each
         time.

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
    static constexpr const char* setChainOrderChannelId = "setChainOrder";
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
        message (see schema/bridge.schema.json SchemaVersion). */
    static constexpr int schemaVersion = 3;

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
    void handleSetChainOrder (const juce::var& event);
    void handleLoadPreset (const juce::var& event);
    void handleSavePresetAs (const juce::var& event);
    void handleSaveCurrentPreset (const juce::var& event);
    void handleNextPreset (const juce::var& event);
    void handlePrevPreset (const juce::var& event);
    void handleRequestPresets (const juce::var& event);

    void pushParamValueToUi (int paramIndex);
    void sendRigState();
    void sendLoadResult (const char* kind, bool ok, const juce::String& message);
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
