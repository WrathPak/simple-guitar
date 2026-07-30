#pragma once

#include <array>
#include <atomic>
#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <sg/Gain.h>
#include <sg/IrLoader.h>
#include <sg/MeterTap.h>
#include <sg/NamProcessor.h>
#include <sg/NoiseGate.h>
#include <sg/PostEq.h>

#include "RigLibrary.h"

/**
    M1 rig processor: input trim -> NoiseGate -> NamProcessor -> PostEq ->
    IrLoader (cab) -> output gain -> MeterTap.

    All engine setters are driven from APVTS atomics once per block (see
    processBlock()) -- the audio thread never touches the APVTS itself,
    only the cached std::atomic<float>* pointers obtained from
    getRawParameterValue() at construction time.

    NAM model / IR loading and the managed library scan happen on the
    message thread; see RigLibrary.h and WebviewBridge for the load/rigState
    flow. The currently-loaded model/IR paths are persisted as properties on
    the APVTS state tree (see getStateInformation/setStateInformation) and
    restored via requestLoad on state load.
*/
class SimpleGuitarAudioProcessor final : public juce::AudioProcessor
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

    /** Every param id above, in a fixed, stable order shared with WebviewBridge. */
    static constexpr int numParams = 12;
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
        cabHighCutHzParamId
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

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Kicks off the state-restore load requests (message thread). Called
        from setStateInformation, marshaled via MessageManager::callAsync
        since the host may call setStateInformation from other threads. */
    void restoreLoadedPathsFromState();

    // Property names used on apvts.state to persist the loaded model/IR path
    // across getStateInformation/setStateInformation (DAW session save/load).
    static constexpr const char* namModelPathPropertyId = "namModelPath";
    static constexpr const char* irPathPropertyId = "irPath";

    //==========================================================================
    // Engine chain: input trim -> gate -> nam -> post eq -> cab -> output gain -> meter.
    sg::Gain inputTrimGain;
    sg::NoiseGate gate;
    sg::NamProcessor nam;
    sg::PostEq postEq;
    sg::IrLoader cab;
    sg::Gain outputGainStage;
    sg::MeterTap meterTap;

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

    // Message-thread-only bookkeeping of the currently-loaded paths, mirrored
    // onto apvts.state properties so they round-trip through getState/setState.
    juce::String currentNamModelPath;
    juce::String currentIrPath;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SimpleGuitarAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleGuitarAudioProcessor)
};
