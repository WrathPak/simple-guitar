#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <optional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>

#include <sg/Gain.h>
#include <sg/IrLoader.h>
#include <sg/MeterTap.h>
#include <sg/NamProcessor.h>
#include <sg/NoiseGate.h>
#include <sg/PlateReverb.h>
#include <sg/PostEq.h>
#include <sg/Screamer.h>
#include <sg/StereoDelay.h>

#include "ChainOrder.h"
#include "PresetStore.h"
#include "RigLibrary.h"

/**
    M2 rig processor: input trim -> NoiseGate -> [three floor pedals, in
    chainOrder] -> NamProcessor -> PostEq -> IrLoader (cab) -> output gain ->
    MeterTap.

    All engine setters are driven from APVTS atomics once per block (see
    processBlock()) -- the audio thread never touches the APVTS itself,
    only the cached std::atomic<float>* pointers obtained from
    getRawParameterValue() at construction time. The three pedals' signal
    order is a separate std::atomic<uint32_t> (see ChainOrder.h) read once
    per block via sg::loadPedalOrder() -- reordering the floor is therefore
    glitch-free and allocation-free, same realtime guarantee as every other
    per-block parameter read here.

    NAM model / IR loading and the managed library scan happen on the
    message thread; see RigLibrary.h and WebviewBridge for the load/rigState
    flow. The currently-loaded model/IR paths and the pedal chain order are
    persisted as properties on the APVTS state tree (see
    getStateInformation/setStateInformation) and restored on state load.

    Screamer is the only pedal that reports nonzero latency
    (getLatencySamples(), fixed once prepare() has run). Total plugin latency
    (setLatencySamples()) must only ever be touched from the message thread
    (JUCE's updateHostDisplay() plumbing behind it is not audio-thread-safe),
    so it's recomputed from prepareToPlay() and from a low-rate message-thread
    timer here (independent of whether an editor/WebviewBridge exists, so
    latency compensation still works for a headless host or pluginval run
    with no editor open) rather than from a parameter listener callback,
    which JUCE may invoke from the audio thread for host automation.

    Presets (see PresetStore.h and the preset-manager section below): a flat
    library of *.sgpreset files under Documents/Simple Guitar/Presets,
    save/load/next/prev all message-thread only. loadPreset() reuses
    setStateInformation's own restore path (apvts.replaceState() +
    restoreLoadedPathsFromState()) so model/IR/chain order restore
    identically whether the state came from a DAW session or a preset file.
    Dirty tracking can't rely on AudioProcessorValueTreeState parameter
    listeners for the same reason updateReportedLatency() above can't --
    JUCE may invoke them from the audio thread for host automation -- so
    "did any param change since the last save/load" is answered the same way
    latency is: a low-rate message-thread poll (see timerCallback()) that
    diffs every param's current normalized value against a baseline snapshot
    taken at the last save/load. Model/IR-load and chain-reorder dirtying are
    set directly at their own call sites instead (not on the poll), since
    those aren't APVTS params.
*/
class SimpleGuitarAudioProcessor final : public juce::AudioProcessor,
                                          private juce::Timer
{
public:
    SimpleGuitarAudioProcessor();
    ~SimpleGuitarAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Parameter ids -- match schema/bridge.schema.json's ParamId enum exactly.
    static constexpr const char* outputGainParamId = "outputGain";
    static constexpr const char* gateOnParamId = "gateOn";
    static constexpr const char* gateThresholdDbParamId = "gateThresholdDb";
    static constexpr const char* ampInputDbParamId = "ampInputDb";
    static constexpr const char* ampBassDbParamId = "ampBassDb";
    static constexpr const char* ampMidDbParamId = "ampMidDb";
    static constexpr const char* ampTrebleDbParamId = "ampTrebleDb";
    static constexpr const char* ampPresenceDbParamId = "ampPresenceDb";
    static constexpr const char* namNormalizeParamId = "namNormalize";
    static constexpr const char* cabOnParamId = "cabOn";
    static constexpr const char* cabLowCutHzParamId = "cabLowCutHz";
    static constexpr const char* cabHighCutHzParamId = "cabHighCutHz";
    static constexpr const char* screamerOnParamId = "screamerOn";
    static constexpr const char* screamerDriveParamId = "screamerDrive";
    static constexpr const char* screamerToneParamId = "screamerTone";
    static constexpr const char* screamerLevelParamId = "screamerLevel";
    static constexpr const char* echoesOnParamId = "echoesOn";
    static constexpr const char* echoesTimeParamId = "echoesTime";
    static constexpr const char* echoesFeedbackParamId = "echoesFeedback";
    static constexpr const char* echoesMixParamId = "echoesMix";
    static constexpr const char* chamberOnParamId = "chamberOn";
    static constexpr const char* chamberDecayParamId = "chamberDecay";
    static constexpr const char* chamberToneParamId = "chamberTone";
    static constexpr const char* chamberMixParamId = "chamberMix";

    /** Every param id above, in a fixed, stable order shared with WebviewBridge. */
    static constexpr int numParams = 24;
    static constexpr std::array<const char*, numParams> allParamIds { {
        outputGainParamId,
        gateOnParamId,
        gateThresholdDbParamId,
        ampInputDbParamId,
        ampBassDbParamId,
        ampMidDbParamId,
        ampTrebleDbParamId,
        ampPresenceDbParamId,
        namNormalizeParamId,
        cabOnParamId,
        cabLowCutHzParamId,
        cabHighCutHzParamId,
        screamerOnParamId,
        screamerDriveParamId,
        screamerToneParamId,
        screamerLevelParamId,
        echoesOnParamId,
        echoesTimeParamId,
        echoesFeedbackParamId,
        echoesMixParamId,
        chamberOnParamId,
        chamberDecayParamId,
        chamberToneParamId,
        chamberMixParamId
    } };

    /** Read-only access to the post-chain meter tap, e.g. for a UI-side timer
        or the webview bridge to push meterFrame events from. */
    sg::MeterTap& getMeterTap() noexcept { return meterTap; }

    /** Engine block access for WebviewBridge (rig state / load requests).
        Message-thread use only -- the audio thread only touches these
        through processBlock()'s own cached-atomic-driven calls. */
    sg::NamProcessor& getNam() noexcept { return nam; }
    sg::IrLoader& getCab() noexcept { return cab; }

    /** Rescans the managed library folders. Message-thread only. */
    sg::RigLibrary scanRigLibrary() const { return sg::scanLibrary(); }

    /** Requests a NAM model load and remembers the path (for state persistence)
        on success. Safe to call only from the message thread. onDone is called
        back on the message thread (per sg::NamProcessor's contract). */
    void requestLoadNamModel (const juce::String& path, std::function<void (bool ok, juce::String errorOrName)> onDone);

    /** Requests an IR load and remembers the path (for state persistence) on
        success. Safe to call only from the message thread; onDone is called
        back synchronously (per sg::IrLoader's contract). */
    void requestLoadIr (const juce::String& path, std::function<void (bool ok, juce::String errorOrName)> onDone);

    //==========================================================================
    // Floor pedal chain order. See ChainOrder.h for the packing scheme; this
    // is the single std::atomic<uint32_t> the audio thread reads once per
    // block (processBlock()) to decide which pedal to run next -- glitch-free,
    // allocation-free reordering.

    /** Thread-safe read of the current chain order (any thread). */
    sg::PedalOrder getChainOrder() const noexcept { return sg::loadPedalOrder (chainOrderAtomic); }

    /** Validates (must be a permutation of the three pedal ids) and applies a
        new chain order, and persists it onto apvts.state for state save/
        restore. Message-thread only (touches apvts.state). Returns false
        (no-op, nothing changed) if `order` isn't a valid permutation. */
    bool setChainOrder (const sg::PedalOrder& order);

    /** schema PedalId <-> sg::PedalSlot mapping, shared by PluginProcessor's
        own state (de)serialization and WebviewBridge's rigState/setChainOrder
        handling. */
    static const char* pedalIdForSlot (sg::PedalSlot slot) noexcept;
    static bool pedalSlotForId (juce::StringRef id, sg::PedalSlot& outSlot) noexcept;

    //==========================================================================
    // Presets. See PresetStore.h for the on-disk format and the class-level
    // comment above for the dirty-tracking design. Message-thread only --
    // same rule as the NAM/IR loaders and setChainOrder above.

    struct CurrentPreset
    {
        juce::String name;
        juce::String path;
    };

    /** Rescans the managed Presets folder, sorted by name. */
    std::vector<sg::PresetEntry> listPresets() const { return sg::scanPresets(); }

    /** The current preset (name+path), if any preset has been saved to or
        loaded this session (or restored from a DAW session's persisted
        currentPresetPath/currentPresetName properties). */
    std::optional<CurrentPreset> getCurrentPreset() const;

    /** True if any APVTS param, the loaded model/IR, or the chain order has
        changed since the last save/load (see the class-level comment for
        how this is tracked). Safe to call from any thread (atomic load),
        though only ever meaningfully written from the message thread. */
    bool isPresetDirty() const noexcept { return presetDirty.load (std::memory_order_relaxed); }

    /** Overwrites the current preset with the current rig state. False (no-op)
        if there is no current preset -- callers should fall back to
        savePresetAs() in that case. */
    bool saveCurrentPreset (juce::String& outError);

    /** Saves (or overwrites) a preset named `rawName` and makes it current.
        `rawName` is sanitized into a safe filename; overwrite-if-a-preset-
        with-that-name-already-exists is intentional, not an error. Fails
        only if nothing sanitizable remains of the name, or the write itself
        fails. */
    bool savePresetAs (const juce::String& rawName, juce::String& outError);

    /** Loads the preset at `path` (must resolve inside the managed Presets
        folder) via the same restore path setStateInformation uses, so
        model/IR/chain order restore identically to a DAW session load. */
    bool loadPreset (const juce::String& path, juce::String& outError);

    /** Steps to the next/previous preset (sorted by name) relative to the
        current one, wrapping around; jumps to the first (next) / last (prev)
        preset if none is current yet. False (no-op) if there are no
        presets. */
    bool nextPreset (juce::String& outError);
    bool prevPreset (juce::String& outError);

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Kicks off the state-restore load requests and restores the chain
        order (message thread). Called from setStateInformation (marshaled
        via MessageManager::callAsync since the host may call
        setStateInformation from other threads) and directly/synchronously
        from loadPreset() (already guaranteed message-thread, so no marshal
        needed there) -- the one shared restore path both go through. */
    void restoreLoadedPathsFromState();

    /** requestLoadNamModel/requestLoadIr's real implementation, with an
        extra `isRestoring` flag: true from restoreLoadedPathsFromState()
        (DAW session restore or a preset load -- must NOT mark the preset
        dirty, since restoring to a known-good state is the opposite of
        dirtying it), false from the public methods below (a genuine
        user-driven load via the bridge -- must mark it dirty on success). */
    void requestLoadNamModelInternal (const juce::String& path, std::function<void (bool, juce::String)> onDone, bool isRestoring);
    void requestLoadIrInternal (const juce::String& path, std::function<void (bool, juce::String)> onDone, bool isRestoring);

    /** Runs one pedal's atomic-driven setters + process() for the given
        slot. Audio-thread only (called from processBlock() in the order
        given by getChainOrder()). */
    void processPedalSlot (sg::PedalSlot slot, juce::AudioBuffer<float>& buffer) noexcept;

    /** Recomputes and (if changed) reports total plugin latency to the host
        via setLatencySamples(). Message-thread only -- see the class-level
        comment for why this must never be called from the audio thread. */
    void updateReportedLatency();

    /** Message-thread poll (see the class-level comment on dirty tracking):
        if not already dirty, compares every param's current normalized value
        against dirtyBaselineValues and marks dirty on the first difference
        found. No-op once already dirty -- nothing to do until the next
        save/load refreshes the baseline. */
    void pollForDirtyParamChange();

    /** Snapshots every param's current normalized value into
        dirtyBaselineValues and clears presetDirty. Called once at
        construction and after every successful save/load (including a DAW
        session restore) -- see the class-level comment. */
    void refreshDirtyBaseline();

    /** Shared body of saveCurrentPreset()/savePresetAs(): serializes the
        current apvts state, writes it to `file` as `name`, and updates the
        current-preset bookkeeping (member vars + apvts.state properties)
        and dirty baseline on success. */
    bool writeCurrentStateToPresetFile (const juce::String& name, const juce::File& file, juce::String& outError);

    void timerCallback() override;

    // Property names used on apvts.state to persist the loaded model/IR path,
    // the pedal chain order, and the current preset across
    // getStateInformation/setStateInformation (DAW session save/load).
    static constexpr const char* namModelPathPropertyId = "namModelPath";
    static constexpr const char* irPathPropertyId = "irPath";
    static constexpr const char* chainOrderPropertyId = "chainOrder";
    static constexpr const char* currentPresetPathPropertyId = "currentPresetPath";
    static constexpr const char* currentPresetNamePropertyId = "currentPresetName";

    /** How often the message-thread latency-refresh timer runs (see class
        comment): only needs to catch on/off toggles, not track anything
        per-frame, so a low rate is plenty. Also drives the dirty-tracking
        poll (pollForDirtyParamChange()) -- same low-rate message-thread
        cadence, same reasoning. */
    static constexpr int latencyRefreshHz = 15;

    //==========================================================================
    // Engine chain: input trim -> gate -> [3 pedals, chainOrder] -> nam -> post eq -> cab -> output gain -> meter.
    sg::Gain inputTrimGain;
    sg::NoiseGate gate;
    sg::Screamer screamer;
    sg::StereoDelay echoes;
    sg::PlateReverb chamber;
    sg::NamProcessor nam;
    sg::PostEq postEq;
    sg::IrLoader cab;
    sg::Gain outputGainStage;
    sg::MeterTap meterTap;

    // Packed pedal order (see ChainOrder.h); starts at the fixed template
    // order (screamer, echoes, chamber) and is restored from apvts.state on
    // setStateInformation.
    std::atomic<std::uint32_t> chainOrderAtomic { sg::packPedalOrder (sg::defaultPedalOrder()) };

    // Cached raw pointers into the APVTS parameters' atomic backing values.
    // Read on the audio thread via atomic load only -- processBlock() never
    // dereferences the AudioProcessorValueTreeState itself.
    std::atomic<float>* outputGainDbParam = nullptr;
    std::atomic<float>* gateOnParam = nullptr;
    std::atomic<float>* gateThresholdDbParam = nullptr;
    std::atomic<float>* ampInputDbParam = nullptr;
    std::atomic<float>* ampBassDbParam = nullptr;
    std::atomic<float>* ampMidDbParam = nullptr;
    std::atomic<float>* ampTrebleDbParam = nullptr;
    std::atomic<float>* ampPresenceDbParam = nullptr;
    std::atomic<float>* namNormalizeParam = nullptr;
    std::atomic<float>* cabOnParam = nullptr;
    std::atomic<float>* cabLowCutHzParam = nullptr;
    std::atomic<float>* cabHighCutHzParam = nullptr;
    std::atomic<float>* screamerOnParam = nullptr;
    std::atomic<float>* screamerDriveParam = nullptr;
    std::atomic<float>* screamerToneParam = nullptr;
    std::atomic<float>* screamerLevelParam = nullptr;
    std::atomic<float>* echoesOnParam = nullptr;
    std::atomic<float>* echoesTimeParam = nullptr;
    std::atomic<float>* echoesFeedbackParam = nullptr;
    std::atomic<float>* echoesMixParam = nullptr;
    std::atomic<float>* chamberOnParam = nullptr;
    std::atomic<float>* chamberDecayParam = nullptr;
    std::atomic<float>* chamberToneParam = nullptr;
    std::atomic<float>* chamberMixParam = nullptr;

    // Message-thread-only bookkeeping of the currently-loaded paths, mirrored
    // onto apvts.state properties so they round-trip through getState/setState.
    juce::String currentNamModelPath;
    juce::String currentIrPath;

    // Message-thread-only bookkeeping of the current preset, mirrored onto
    // apvts.state properties (currentPresetPathPropertyId/NamePropertyId) so
    // a DAW session remembers which preset it was on.
    juce::String currentPresetName;
    juce::String currentPresetPath;

    // Dirty tracking (see the class-level comment). Set true directly at the
    // model/IR-load and chain-reorder call sites; set true for a plain
    // param change by pollForDirtyParamChange() diffing against
    // dirtyBaselineValues. Cleared (along with a fresh baseline snapshot) by
    // refreshDirtyBaseline() on construction and after every successful
    // save/load. presetDirty is atomic since WebviewBridge (a different
    // object, but also message-thread-only) reads it; dirtyBaselineValues is
    // plain since only this processor's own message-thread code ever
    // touches it.
    std::atomic<bool> presetDirty { false };
    std::array<float, numParams> dirtyBaselineValues {};

    JUCE_DECLARE_WEAK_REFERENCEABLE (SimpleGuitarAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleGuitarAudioProcessor)
};
