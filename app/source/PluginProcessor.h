#pragma once

#include <array>
#include <atomic>
#include <functional>

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

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Kicks off the state-restore load requests and restores the chain
        order (message thread). Called from setStateInformation, marshaled
        via MessageManager::callAsync since the host may call
        setStateInformation from other threads. */
    void restoreLoadedPathsFromState();

    /** Runs one pedal's atomic-driven setters + process() for the given
        slot. Audio-thread only (called from processBlock() in the order
        given by getChainOrder()). */
    void processPedalSlot (sg::PedalSlot slot, juce::AudioBuffer<float>& buffer) noexcept;

    /** Recomputes and (if changed) reports total plugin latency to the host
        via setLatencySamples(). Message-thread only -- see the class-level
        comment for why this must never be called from the audio thread. */
    void updateReportedLatency();

    void timerCallback() override;

    // Property names used on apvts.state to persist the loaded model/IR path
    // and the pedal chain order across getStateInformation/setStateInformation
    // (DAW session save/load).
    static constexpr const char* namModelPathPropertyId = "namModelPath";
    static constexpr const char* irPathPropertyId = "irPath";
    static constexpr const char* chainOrderPropertyId = "chainOrder";

    /** How often the message-thread latency-refresh timer runs (see class
        comment): only needs to catch on/off toggles, not track anything
        per-frame, so a low rate is plenty. */
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

    JUCE_DECLARE_WEAK_REFERENCEABLE (SimpleGuitarAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleGuitarAudioProcessor)
};
