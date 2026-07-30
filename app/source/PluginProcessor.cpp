#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "ContentInstaller.h"
#include "StateMigration.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include <juce_events/juce_events.h>

namespace
{
    // True logarithmic (equal-ratio) frequency mapping for the cab hi/lo cut
    // knobs, so the knob feels linear in pitch/octaves rather than linear in Hz.
    juce::NormalisableRange<float> logFrequencyRange (float minHz, float maxHz)
    {
        return juce::NormalisableRange<float> (
            minHz,
            maxHz,
            [] (float start, float end, float normalised)
            {
                return start * std::pow (end / start, normalised);
            },
            [] (float start, float end, float value)
            {
                return std::log (value / start) / std::log (end / start);
            });
    }

    // Display names for the slot{N}Type AudioParameterChoice -- order must
    // match sg::PedalType exactly (index == the enum's underlying value).
    const juce::StringArray& pedalTypeChoiceNames()
    {
        static const juce::StringArray names { "Empty", "Screamer", "Echoes", "Chamber", "Squeeze", "Wobble", "Shiver" };
        return names;
    }

    // Default slot occupants on a fresh install: the old fixed trio filling
    // the first three physical slots (screamer, echoes, chamber), the rest
    // empty. On/P1-4 defaults are flat/uniform across every slot regardless
    // of type (true / 0.5) -- set directly in createParameterLayout() below.
    constexpr std::array<int, sg::numSlots> defaultSlotTypes { { 1, 2, 3, 0, 0, 0 } };
}

SimpleGuitarAudioProcessor::SimpleGuitarAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    outputGainDbParam = apvts.getRawParameterValue (outputGainParamId);
    gateOnParam = apvts.getRawParameterValue (gateOnParamId);
    gateThresholdDbParam = apvts.getRawParameterValue (gateThresholdDbParamId);
    ampInputDbParam = apvts.getRawParameterValue (ampInputDbParamId);
    ampBassDbParam = apvts.getRawParameterValue (ampBassDbParamId);
    ampMidDbParam = apvts.getRawParameterValue (ampMidDbParamId);
    ampTrebleDbParam = apvts.getRawParameterValue (ampTrebleDbParamId);
    ampPresenceDbParam = apvts.getRawParameterValue (ampPresenceDbParamId);
    namNormalizeParam = apvts.getRawParameterValue (namNormalizeParamId);
    cabOnParam = apvts.getRawParameterValue (cabOnParamId);
    cabLowCutHzParam = apvts.getRawParameterValue (cabLowCutHzParamId);
    cabHighCutHzParam = apvts.getRawParameterValue (cabHighCutHzParamId);

    for (int slot = 0; slot < sg::numSlots; ++slot)
    {
        slotTypeParams[(std::size_t) slot] = apvts.getRawParameterValue (sg::slotParamId (slot, "Type"));
        slotOnParams[(std::size_t) slot] = apvts.getRawParameterValue (sg::slotParamId (slot, "On"));

        for (int p = 0; p < sg::numParamsPerSlot; ++p)
            slotPParams[(std::size_t) slot][(std::size_t) p] =
                apvts.getRawParameterValue (sg::slotParamId (slot, "P" + juce::String (p + 1)));
    }

    lastKnownSlotType.fill (-1);

    // Message-thread work: make sure the managed library folders exist from
    // the moment the plugin is instantiated, so the first rigState/
    // presetsState the UI asks for already has somewhere to scan.
    sg::ensureLibraryFoldersExist();
    sg::ensurePresetsFolderExists();

    // First-run install of the bundled factory content (models/IRs/presets,
    // embedded as BinaryData -- see app/CMakeLists.txt and ContentInstaller.h).
    // Copy-if-absent per file, so this is cheap and safe to run on every
    // startup, not just a true first run.
    sg::installBundledContent();

    // Baseline for dirty-tracking (see the class-level comment): freshly
    // constructed, at the default param values, reads as "not dirty".
    refreshDirtyBaseline();

    startTimerHz (latencyRefreshHz);
}

SimpleGuitarAudioProcessor::~SimpleGuitarAudioProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout SimpleGuitarAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { outputGainParamId, 1 },
        "Output Gain",
        juce::NormalisableRange<float> (-60.0f, 12.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { gateOnParamId, 1 },
        "Gate",
        true));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { gateThresholdDbParamId, 1 },
        "Gate Threshold",
        juce::NormalisableRange<float> (-90.0f, -20.0f, 0.01f),
        -60.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ampInputDbParamId, 1 },
        "Input Trim",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ampBassDbParamId, 1 },
        "Bass",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ampMidDbParamId, 1 },
        "Mid",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ampTrebleDbParamId, 1 },
        "Treble",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ampPresenceDbParamId, 1 },
        "Presence",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { namNormalizeParamId, 1 },
        "Normalize",
        true));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { cabOnParamId, 1 },
        "Cab",
        true));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { cabLowCutHzParamId, 1 },
        "Low Cut",
        logFrequencyRange (20.0f, 400.0f),
        60.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { cabHighCutHzParamId, 1 },
        "High Cut",
        logFrequencyRange (2000.0f, 20000.0f),
        8000.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    // Pedal slots: 6 generic slots, each a {type, on, P1..P4} group. Type is
    // a 7-way choice (0 empty .. 6 shiver, see sg::PedalType); On/P1-4 are
    // flat-default (true / 0.5) regardless of type -- a slot's DSP instance
    // (built on the message thread, see PedalSlots.h/PluginProcessor's
    // applySlotTypeIfChanged()) owns the actual per-type real-unit mapping.
    for (int slot = 0; slot < sg::numSlots; ++slot)
    {
        const juce::String slotLabel = "Slot " + juce::String (slot + 1);

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { sg::slotParamId (slot, "Type"), 1 },
            slotLabel + " Type",
            pedalTypeChoiceNames(),
            defaultSlotTypes[(std::size_t) slot]));

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { sg::slotParamId (slot, "On"), 1 },
            slotLabel + " On",
            true));

        for (int p = 1; p <= sg::numParamsPerSlot; ++p)
        {
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { sg::slotParamId (slot, "P" + juce::String (p)), 1 },
                slotLabel + " P" + juce::String (p),
                juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
                0.5f));
        }
    }

    return { params.begin(), params.end() };
}

void SimpleGuitarAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto numChannels = getTotalNumOutputChannels();

    preparedSampleRate = sampleRate;
    preparedBlockSize = samplesPerBlock;
    preparedNumChannels = numChannels;

    inputTrimGain.prepare (sampleRate, samplesPerBlock, numChannels);
    gate.prepare (sampleRate, samplesPerBlock, numChannels);

    std::array<sg::PedalType, sg::numSlots> types {};
    for (int slot = 0; slot < sg::numSlots; ++slot)
    {
        const int raw = getSlotType (slot);
        types[(std::size_t) slot] = (sg::PedalType) raw;
        lastKnownSlotType[(std::size_t) slot] = raw;
    }
    slotHost.prepare (sampleRate, samplesPerBlock, numChannels, types);
    slotHostPrepared = true;

    nam.prepare (sampleRate, samplesPerBlock);
    postEq.prepare (sampleRate, samplesPerBlock, numChannels);
    cab.prepare (sampleRate, samplesPerBlock, numChannels);
    outputGainStage.prepare (sampleRate, samplesPerBlock, numChannels);
    meterTap.reset();

    // Screamer-type slots' latency (getLatencySamples()) is only valid once
    // prepare() has run; refresh the reported host latency now that it is.
    updateReportedLatency();
}

void SimpleGuitarAudioProcessor::releaseResources()
{
}

bool SimpleGuitarAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void SimpleGuitarAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // All engine setters below are atomic loads only -- no lock, no
    // allocation, no APVTS access on the audio thread.

    // 1. Input trim.
    inputTrimGain.setGainDecibels (ampInputDbParam->load (std::memory_order_relaxed));
    {
        juce::dsp::AudioBlock<float> block (buffer);
        inputTrimGain.process (block);
    }

    // 2. Noise gate.
    gate.setEnabled (gateOnParam->load (std::memory_order_relaxed) >= 0.5f);
    gate.setThresholdDb (gateThresholdDbParam->load (std::memory_order_relaxed));
    gate.process (buffer);

    // 2.5. Pedal slots, in slot-index order -- slot index IS signal order
    // now (see PluginProcessor.h's class-level comment); no separate chain-
    // order concept. Each slot reads its own on/off + four knob atomics and
    // hands them to PedalSlotHost, which drives whichever DSP instance is
    // (or is mid-crossfade into being) live for that slot.
    for (int slot = 0; slot < sg::numSlots; ++slot)
    {
        const bool on = slotOnParams[(std::size_t) slot]->load (std::memory_order_relaxed) >= 0.5f;

        std::array<float, sg::numParamsPerSlot> slotParams {};
        for (int p = 0; p < sg::numParamsPerSlot; ++p)
            slotParams[(std::size_t) p] = slotPParams[(std::size_t) slot][(std::size_t) p]->load (std::memory_order_relaxed);

        slotHost.processSlot (slot, on, slotParams, buffer);
    }

    // 3. NAM amp.
    nam.setNormalize (namNormalizeParam->load (std::memory_order_relaxed) >= 0.5f);
    nam.process (buffer);

    // 4. Post-EQ (Bass/Mid/Treble/Presence, integrated into the amp faceplate).
    postEq.setBassDb (ampBassDbParam->load (std::memory_order_relaxed));
    postEq.setMidDb (ampMidDbParam->load (std::memory_order_relaxed));
    postEq.setTrebleDb (ampTrebleDbParam->load (std::memory_order_relaxed));
    postEq.setPresenceDb (ampPresenceDbParam->load (std::memory_order_relaxed));
    postEq.process (buffer);

    // 5. Cab IR.
    cab.setEnabled (cabOnParam->load (std::memory_order_relaxed) >= 0.5f);
    cab.setLowCutHz (cabLowCutHzParam->load (std::memory_order_relaxed));
    cab.setHighCutHz (cabHighCutHzParam->load (std::memory_order_relaxed));
    cab.process (buffer);

    // 6. Output gain.
    outputGainStage.setGainDecibels (outputGainDbParam->load (std::memory_order_relaxed));
    {
        juce::dsp::AudioBlock<float> block (buffer);
        outputGainStage.process (block);
    }

    // 7. Meter tap (post-chain).
    meterTap.pushBlock (buffer.getArrayOfReadPointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

int SimpleGuitarAudioProcessor::getSlotType (int slot) const noexcept
{
    if (! sg::isValidSlotIndex (slot))
        return 0;

    const int raw = (int) std::lround (slotTypeParams[(std::size_t) slot]->load (std::memory_order_relaxed));
    return juce::jlimit (0, sg::numPedalTypes - 1, raw);
}

bool SimpleGuitarAudioProcessor::getSlotOn (int slot) const noexcept
{
    if (! sg::isValidSlotIndex (slot))
        return false;

    return slotOnParams[(std::size_t) slot]->load (std::memory_order_relaxed) >= 0.5f;
}

void SimpleGuitarAudioProcessor::applySlotTypeIfChanged (int slot)
{
    jassert (sg::isValidSlotIndex (slot));

    const int currentType = getSlotType (slot);
    if (currentType == lastKnownSlotType[(std::size_t) slot])
        return;

    lastKnownSlotType[(std::size_t) slot] = currentType;

    if (! slotHostPrepared)
        return; // nothing to rebuild yet -- prepareToPlay()'s own initial build already uses the current values.

    auto retired = slotHost.swapSlot (slot, (sg::PedalType) currentType,
                                       preparedSampleRate, preparedBlockSize, preparedNumChannels);

    retiringPedals.push_back ({ std::move (retired), juce::Time::getMillisecondCounter() });
}

void SimpleGuitarAudioProcessor::syncAllSlotInstancesFromParams()
{
    for (int slot = 0; slot < sg::numSlots; ++slot)
        applySlotTypeIfChanged (slot);
}

void SimpleGuitarAudioProcessor::collectRetiredPedals()
{
    const auto now = juce::Time::getMillisecondCounter();

    retiringPedals.erase (
        std::remove_if (retiringPedals.begin(), retiringPedals.end(),
            [now] (const RetiringPedal& r) { return (now - r.retiredAtMs) >= retireGraceMs; }),
        retiringPedals.end());
}

bool SimpleGuitarAudioProcessor::setSlotType (int slot, int pedalType)
{
    if (! sg::isValidSlotIndex (slot) || ! sg::isValidPedalType (pedalType))
        return false;

    auto* typeParam = apvts.getParameter (sg::slotParamId (slot, "Type"));
    auto* onParam = apvts.getParameter (sg::slotParamId (slot, "On"));
    auto* p1Param = apvts.getParameter (sg::slotParamId (slot, "P1"));
    auto* p2Param = apvts.getParameter (sg::slotParamId (slot, "P2"));
    auto* p3Param = apvts.getParameter (sg::slotParamId (slot, "P3"));
    auto* p4Param = apvts.getParameter (sg::slotParamId (slot, "P4"));

    if (typeParam == nullptr || onParam == nullptr
        || p1Param == nullptr || p2Param == nullptr || p3Param == nullptr || p4Param == nullptr)
        return false; // shouldn't happen -- defensive only

    // A genuine "pick a new pedal for this slot" gesture resets On/P1-4 to
    // their flat defaults -- see the class-level comment in PluginProcessor.h.
    typeParam->setValueNotifyingHost (typeParam->convertTo0to1 ((float) pedalType));
    onParam->setValueNotifyingHost (onParam->convertTo0to1 (1.0f));
    p1Param->setValueNotifyingHost (p1Param->convertTo0to1 (0.5f));
    p2Param->setValueNotifyingHost (p2Param->convertTo0to1 (0.5f));
    p3Param->setValueNotifyingHost (p3Param->convertTo0to1 (0.5f));
    p4Param->setValueNotifyingHost (p4Param->convertTo0to1 (0.5f));

    applySlotTypeIfChanged (slot);

    presetDirty.store (true, std::memory_order_relaxed);
    return true;
}

bool SimpleGuitarAudioProcessor::movePedal (int from, int to)
{
    if (! sg::isValidSlotIndex (from) || ! sg::isValidSlotIndex (to))
        return false;

    if (from == to)
        return true; // harmless no-op

    struct SlotValues
    {
        float type, on, p1, p2, p3, p4;
    };

    std::array<SlotValues, sg::numSlots> snapshot {};

    for (int i = 0; i < sg::numSlots; ++i)
    {
        snapshot[(std::size_t) i].type = slotTypeParams[(std::size_t) i]->load (std::memory_order_relaxed);
        snapshot[(std::size_t) i].on = slotOnParams[(std::size_t) i]->load (std::memory_order_relaxed);
        snapshot[(std::size_t) i].p1 = slotPParams[(std::size_t) i][0]->load (std::memory_order_relaxed);
        snapshot[(std::size_t) i].p2 = slotPParams[(std::size_t) i][1]->load (std::memory_order_relaxed);
        snapshot[(std::size_t) i].p3 = slotPParams[(std::size_t) i][2]->load (std::memory_order_relaxed);
        snapshot[(std::size_t) i].p4 = slotPParams[(std::size_t) i][3]->load (std::memory_order_relaxed);
    }

    const auto perm = sg::computeMovePermutation (from, to);

    for (int i = 0; i < sg::numSlots; ++i)
    {
        const auto& src = snapshot[(std::size_t) perm[(std::size_t) i]];

        auto* typeParam = apvts.getParameter (sg::slotParamId (i, "Type"));
        auto* onParam = apvts.getParameter (sg::slotParamId (i, "On"));
        auto* p1Param = apvts.getParameter (sg::slotParamId (i, "P1"));
        auto* p2Param = apvts.getParameter (sg::slotParamId (i, "P2"));
        auto* p3Param = apvts.getParameter (sg::slotParamId (i, "P3"));
        auto* p4Param = apvts.getParameter (sg::slotParamId (i, "P4"));

        if (typeParam == nullptr || onParam == nullptr
            || p1Param == nullptr || p2Param == nullptr || p3Param == nullptr || p4Param == nullptr)
            continue; // shouldn't happen -- defensive only

        // movePedal moves param VALUES, not DSP instances -- On/P1-4 travel
        // with the pedal unchanged (unlike setSlotType(), which resets them).
        typeParam->setValueNotifyingHost (typeParam->convertTo0to1 (src.type));
        onParam->setValueNotifyingHost (onParam->convertTo0to1 (src.on));
        p1Param->setValueNotifyingHost (p1Param->convertTo0to1 (src.p1));
        p2Param->setValueNotifyingHost (p2Param->convertTo0to1 (src.p2));
        p3Param->setValueNotifyingHost (p3Param->convertTo0to1 (src.p3));
        p4Param->setValueNotifyingHost (p4Param->convertTo0to1 (src.p4));
    }

    // Rebuild whichever slots' TYPE value actually ended up different --
    // no-op for any slot whose type happened to land back on itself.
    for (int i = 0; i < sg::numSlots; ++i)
        applySlotTypeIfChanged (i);

    presetDirty.store (true, std::memory_order_relaxed);
    return true;
}

void SimpleGuitarAudioProcessor::updateReportedLatency()
{
    // Only screamer-type slots contribute latency (their internal 2x
    // oversampling; see engine/include/sg/Screamer.h); every other type
    // (including empty) reports 0 from PedalInstance::getLatencySamples().
    // A bypassed (off) slot contributes nothing either, matching the old
    // single-screamer contract: bypass is a bit-exact passthrough, not an
    // actually-delayed signal, so reporting host PDC latency for it would be
    // wrong. Order-independent (serial-chain latencies just sum), so this
    // doesn't need to know slot order beyond "sum all six".
    int total = 0;
    for (int slot = 0; slot < sg::numSlots; ++slot)
        total += slotHost.getReportedLatencySamples (slot, getSlotOn (slot));

    if (total != getLatencySamples())
        setLatencySamples (total);
}

void SimpleGuitarAudioProcessor::timerCallback()
{
    // Low-rate message-thread poll -- see the class-level comment in
    // PluginProcessor.h for why none of this can be a parameter listener
    // (JUCE may invoke those from the audio thread for host automation) and
    // must instead be a message-thread refresh that works whether or not an
    // editor/WebviewBridge exists.
    updateReportedLatency();
    pollForDirtyParamChange();

    // Backstop for a slot{N}Type param changed by host automation directly
    // (setSlotType()/movePedal() already applied their own changes
    // synchronously above, so this is a no-op for those).
    syncAllSlotInstancesFromParams();

    // Deferred deletion of any pedal instance retired by a slot swap whose
    // grace period has now elapsed (never on the audio thread).
    collectRetiredPedals();
}

void SimpleGuitarAudioProcessor::pollForDirtyParamChange()
{
    if (presetDirty.load (std::memory_order_relaxed))
        return; // already dirty -- nothing to do until the next save/load.

    for (std::size_t i = 0; i < (std::size_t) numParams; ++i)
    {
        auto* param = apvts.getParameter (allParamIds[i]);
        if (param == nullptr)
            continue;

        if (! juce::approximatelyEqual (param->getValue(), dirtyBaselineValues[i]))
        {
            presetDirty.store (true, std::memory_order_relaxed);
            return;
        }
    }
}

void SimpleGuitarAudioProcessor::refreshDirtyBaseline()
{
    for (std::size_t i = 0; i < (std::size_t) numParams; ++i)
    {
        if (auto* param = apvts.getParameter (allParamIds[i]))
            dirtyBaselineValues[i] = param->getValue();
    }

    presetDirty.store (false, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* SimpleGuitarAudioProcessor::createEditor()
{
    return new SimpleGuitarAudioProcessorEditor (*this);
}

bool SimpleGuitarAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String SimpleGuitarAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SimpleGuitarAudioProcessor::acceptsMidi() const
{
    return false;
}

bool SimpleGuitarAudioProcessor::producesMidi() const
{
    return false;
}

bool SimpleGuitarAudioProcessor::isMidiEffect() const
{
    return false;
}

double SimpleGuitarAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SimpleGuitarAudioProcessor::getNumPrograms()
{
    return 1;
}

int SimpleGuitarAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SimpleGuitarAudioProcessor::setCurrentProgram (int)
{
}

const juce::String SimpleGuitarAudioProcessor::getProgramName (int)
{
    return {};
}

void SimpleGuitarAudioProcessor::changeProgramName (int, const juce::String&)
{
}

void SimpleGuitarAudioProcessor::requestLoadNamModel (const juce::String& path, std::function<void (bool, juce::String)> onDone)
{
    requestLoadNamModelInternal (path, std::move (onDone), false);
}

void SimpleGuitarAudioProcessor::requestLoadIr (const juce::String& path, std::function<void (bool, juce::String)> onDone)
{
    requestLoadIrInternal (path, std::move (onDone), false);
}

void SimpleGuitarAudioProcessor::requestLoadNamModelInternal (const juce::String& path, std::function<void (bool, juce::String)> onDone, bool isRestoring)
{
    juce::WeakReference<SimpleGuitarAudioProcessor> safeThis (this);

    nam.requestLoad (path, [safeThis, path, onDone, isRestoring] (bool ok, juce::String errorOrName)
    {
        if (auto* self = safeThis.get())
        {
            if (ok)
            {
                self->currentNamModelPath = path;
                self->apvts.state.setProperty (namModelPathPropertyId, path, nullptr);

                // Only a genuine user-driven load (via the bridge) dirties
                // the preset -- restoring to a saved/loaded state must not
                // immediately mark it dirty again (see the class-level
                // comment).
                if (! isRestoring)
                    self->presetDirty.store (true, std::memory_order_relaxed);
            }
        }

        if (onDone)
            onDone (ok, errorOrName);
    });
}

void SimpleGuitarAudioProcessor::requestLoadIrInternal (const juce::String& path, std::function<void (bool, juce::String)> onDone, bool isRestoring)
{
    juce::WeakReference<SimpleGuitarAudioProcessor> safeThis (this);

    cab.requestLoad (path, [safeThis, path, onDone, isRestoring] (bool ok, juce::String errorOrName)
    {
        if (auto* self = safeThis.get())
        {
            if (ok)
            {
                self->currentIrPath = path;
                self->apvts.state.setProperty (irPathPropertyId, path, nullptr);

                if (! isRestoring)
                    self->presetDirty.store (true, std::memory_order_relaxed);
            }
        }

        if (onDone)
            onDone (ok, errorOrName);
    });
}

void SimpleGuitarAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    const std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SimpleGuitarAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState == nullptr || ! xmlState->hasTagName (apvts.state.getType()))
        return;

    // Transparently upgrades a pre-slot-architecture state (fixed screamer/
    // echoes/chamber params + chainOrder attribute) into the current slot-
    // param shape; a no-op clone for an already-current-format state (see
    // StateMigration.h).
    auto migrated = sg::migrateStateXmlIfNeeded (*xmlState);
    apvts.replaceState (juce::ValueTree::fromXml (*migrated));

    // The host can call setStateInformation from threads other than the
    // message thread (e.g. during project load); requestLoad on the engine
    // blocks must happen on the message thread, so marshal it there. Guard
    // against the processor being destroyed before the async call runs with
    // a weak reference.
    juce::WeakReference<SimpleGuitarAudioProcessor> safeThis (this);

    juce::MessageManager::callAsync ([safeThis]
    {
        if (auto* self = safeThis.get())
        {
            self->restoreLoadedPathsFromState();

            // A freshly-restored DAW session reads as "not dirty" -- it
            // matches its own saved state by definition.
            self->refreshDirtyBaseline();
        }
    });
}

void SimpleGuitarAudioProcessor::restoreLoadedPathsFromState()
{
    const auto namPath = apvts.state.getProperty (namModelPathPropertyId, "").toString();
    const auto irPath = apvts.state.getProperty (irPathPropertyId, "").toString();

    if (namPath.isNotEmpty() && namPath != currentNamModelPath)
        requestLoadNamModelInternal (namPath, nullptr, true);

    if (irPath.isNotEmpty() && irPath != currentIrPath)
        requestLoadIrInternal (irPath, nullptr, true);

    // Pedal slots: the state tree already carries the persisted (or, for a
    // legacy state, migrated -- see StateMigration.h) slot param values
    // verbatim (they just came in via apvts.replaceState()); this only
    // needs to rebuild whichever slots' DSP instances no longer match.
    syncAllSlotInstancesFromParams();

    // Current preset bookkeeping: plain string properties, no filesystem
    // access needed to "restore" them (unlike nam/ir, which must re-run the
    // loader) -- restored here so a DAW session remembers which preset it
    // was on (chrome preset pill) across project save/reload.
    currentPresetPath = apvts.state.getProperty (currentPresetPathPropertyId, "").toString();
    currentPresetName = apvts.state.getProperty (currentPresetNamePropertyId, "").toString();
}

//==============================================================================
// Presets. See PresetStore.h for the on-disk format and PluginProcessor.h's
// class-level comment for the dirty-tracking design.

std::optional<SimpleGuitarAudioProcessor::CurrentPreset> SimpleGuitarAudioProcessor::getCurrentPreset() const
{
    if (currentPresetPath.isEmpty())
        return std::nullopt;

    return CurrentPreset { currentPresetName, currentPresetPath };
}

bool SimpleGuitarAudioProcessor::writeCurrentStateToPresetFile (const juce::String& name, const juce::File& file, juce::String& outError)
{
    const auto state = apvts.copyState();
    const std::unique_ptr<juce::XmlElement> xml (state.createXml());

    if (xml == nullptr)
    {
        outError = "couldn't serialize the current state";
        return false;
    }

    sg::ensurePresetsFolderExists();

    if (! sg::writePresetFile (file, name, xml->toString()))
    {
        outError = "couldn't write the preset file";
        return false;
    }

    currentPresetName = name;
    currentPresetPath = file.getFullPathName();
    apvts.state.setProperty (currentPresetPathPropertyId, currentPresetPath, nullptr);
    apvts.state.setProperty (currentPresetNamePropertyId, currentPresetName, nullptr);

    // Saving clears dirty, same as loading.
    refreshDirtyBaseline();
    return true;
}

bool SimpleGuitarAudioProcessor::saveCurrentPreset (juce::String& outError)
{
    if (currentPresetPath.isEmpty())
    {
        outError = "no current preset -- use save as";
        return false;
    }

    return writeCurrentStateToPresetFile (currentPresetName, juce::File (currentPresetPath), outError);
}

bool SimpleGuitarAudioProcessor::savePresetAs (const juce::String& rawName, juce::String& outError)
{
    const auto sanitized = sg::sanitizePresetFilename (rawName);

    if (sanitized.isEmpty())
    {
        outError = "name required";
        return false;
    }

    sg::ensurePresetsFolderExists();
    const auto file = sg::getPresetsLibraryFolder().getChildFile (sanitized + ".sgpreset");

    // Overwrite-if-a-preset-with-that-name-already-exists is intentional
    // (see PresetStore.h / PluginProcessor.h) -- writeCurrentStateToPresetFile
    // doesn't distinguish "new file" from "overwrite".
    return writeCurrentStateToPresetFile (rawName.trim(), file, outError);
}

bool SimpleGuitarAudioProcessor::loadPreset (const juce::String& path, juce::String& outError)
{
    const juce::File file (path);

    if (! sg::isInsidePresetsFolder (file))
    {
        outError = "path is outside the managed Presets folder";
        return false;
    }

    sg::PresetFileContents contents;

    if (! sg::readPresetFile (file, contents))
    {
        outError = "couldn't read preset (missing, corrupt, or wrong format)";
        return false;
    }

    const std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (contents.stateXmlText));

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
    {
        outError = "preset's saved state is corrupt";
        return false;
    }

    // Transparently upgrades a legacy-format preset (see StateMigration.h);
    // a no-op clone for an already-current-format preset.
    auto migrated = sg::migrateStateXmlIfNeeded (*xml);

    // Same restore path setStateInformation uses (apvts.replaceState() +
    // restoreLoadedPathsFromState()), called directly/synchronously rather
    // than marshaled via MessageManager::callAsync -- loadPreset() is
    // documented message-thread-only (unlike setStateInformation, which the
    // host may call from any thread), so there's no cross-thread hazard to
    // guard against here, and a synchronous restore means the presetsState
    // WebviewBridge sends right after this returns is already accurate.
    apvts.replaceState (juce::ValueTree::fromXml (*migrated));
    restoreLoadedPathsFromState();

    // Restoring this preset's own saved paths above just set
    // currentPresetPath/Name back from whatever the *previous* current
    // preset's properties were (or empty, carried over inside the loaded
    // state) -- overwrite with the preset actually being loaded now.
    currentPresetName = contents.name;
    currentPresetPath = file.getFullPathName();
    apvts.state.setProperty (currentPresetPathPropertyId, currentPresetPath, nullptr);
    apvts.state.setProperty (currentPresetNamePropertyId, currentPresetName, nullptr);

    // Loading clears dirty, same as saving.
    refreshDirtyBaseline();
    return true;
}

bool SimpleGuitarAudioProcessor::nextPreset (juce::String& outError)
{
    const auto presets = sg::scanPresets();

    if (presets.empty())
    {
        outError = "no presets";
        return false;
    }

    std::size_t targetIndex = 0;

    if (currentPresetPath.isNotEmpty())
    {
        const auto it = std::find_if (presets.begin(), presets.end(),
            [this] (const sg::PresetEntry& e) { return e.path == currentPresetPath; });

        if (it != presets.end())
            targetIndex = ((std::size_t) std::distance (presets.begin(), it) + 1) % presets.size();
        // else: current preset no longer exists on disk -- fall back to the first.
    }

    return loadPreset (presets[targetIndex].path, outError);
}

bool SimpleGuitarAudioProcessor::prevPreset (juce::String& outError)
{
    const auto presets = sg::scanPresets();

    if (presets.empty())
    {
        outError = "no presets";
        return false;
    }

    std::size_t targetIndex = presets.size() - 1;

    if (currentPresetPath.isNotEmpty())
    {
        const auto it = std::find_if (presets.begin(), presets.end(),
            [this] (const sg::PresetEntry& e) { return e.path == currentPresetPath; });

        if (it != presets.end())
        {
            const auto index = (std::size_t) std::distance (presets.begin(), it);
            targetIndex = (index == 0) ? presets.size() - 1 : index - 1;
        }
        // else: current preset no longer exists on disk -- fall back to the last.
    }

    return loadPreset (presets[targetIndex].path, outError);
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpleGuitarAudioProcessor();
}
