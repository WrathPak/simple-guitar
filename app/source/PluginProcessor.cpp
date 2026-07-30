#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "ContentInstaller.h"

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

    // Chain-order <-> apvts.state property text, e.g. "screamer,echoes,chamber".
    // Same spirit as namModelPath/irPath: a plain comma-joined string property
    // on apvts.state, restored via restoreLoadedPathsFromState().
    juce::String chainOrderToString (const sg::PedalOrder& order)
    {
        juce::StringArray ids;
        for (auto slot : order)
            ids.add (SimpleGuitarAudioProcessor::pedalIdForSlot (slot));
        return ids.joinIntoString (",");
    }

    bool parseChainOrderString (const juce::String& text, sg::PedalOrder& outOrder)
    {
        const auto ids = juce::StringArray::fromTokens (text, ",", "");
        if (ids.size() != sg::numPedalSlots)
            return false;

        sg::PedalOrder order {};
        for (int i = 0; i < sg::numPedalSlots; ++i)
        {
            if (! SimpleGuitarAudioProcessor::pedalSlotForId (ids[i], order[(std::size_t) i]))
                return false;
        }

        if (! sg::isValidPedalOrder (order))
            return false;

        outOrder = order;
        return true;
    }
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
    screamerOnParam = apvts.getRawParameterValue (screamerOnParamId);
    screamerDriveParam = apvts.getRawParameterValue (screamerDriveParamId);
    screamerToneParam = apvts.getRawParameterValue (screamerToneParamId);
    screamerLevelParam = apvts.getRawParameterValue (screamerLevelParamId);
    echoesOnParam = apvts.getRawParameterValue (echoesOnParamId);
    echoesTimeParam = apvts.getRawParameterValue (echoesTimeParamId);
    echoesFeedbackParam = apvts.getRawParameterValue (echoesFeedbackParamId);
    echoesMixParam = apvts.getRawParameterValue (echoesMixParamId);
    chamberOnParam = apvts.getRawParameterValue (chamberOnParamId);
    chamberDecayParam = apvts.getRawParameterValue (chamberDecayParamId);
    chamberToneParam = apvts.getRawParameterValue (chamberToneParamId);
    chamberMixParam = apvts.getRawParameterValue (chamberMixParamId);

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

    // Floor pedals. The nine continuous params below are plain 0..1
    // normalized floats end to end -- no dB/Hz range like the M1 rig params
    // above, since sg::Screamer/StereoDelay/PlateReverb take normalized
    // setters directly and own the real-unit mapping internally (see
    // engine/include/sg/{Screamer,StereoDelay,PlateReverb}.h).
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { screamerOnParamId, 1 },
        "Screamer",
        false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { screamerDriveParamId, 1 },
        "Screamer Drive",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { screamerToneParamId, 1 },
        "Screamer Tone",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { screamerLevelParamId, 1 },
        "Screamer Level",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.7f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { echoesOnParamId, 1 },
        "Echoes",
        true));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { echoesTimeParamId, 1 },
        "Echoes Time",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { echoesFeedbackParamId, 1 },
        "Echoes Feedback",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.4f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { echoesMixParamId, 1 },
        "Echoes Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.25f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { chamberOnParamId, 1 },
        "Chamber",
        true));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { chamberDecayParamId, 1 },
        "Chamber Decay",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { chamberToneParamId, 1 },
        "Chamber Tone",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { chamberMixParamId, 1 },
        "Chamber Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.22f));

    return { params.begin(), params.end() };
}

void SimpleGuitarAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto numChannels = getTotalNumOutputChannels();

    inputTrimGain.prepare (sampleRate, samplesPerBlock, numChannels);
    gate.prepare (sampleRate, samplesPerBlock, numChannels);
    screamer.prepare (sampleRate, samplesPerBlock, numChannels);
    echoes.prepare (sampleRate, samplesPerBlock, numChannels);
    chamber.prepare (sampleRate, samplesPerBlock, numChannels);
    nam.prepare (sampleRate, samplesPerBlock);
    postEq.prepare (sampleRate, samplesPerBlock, numChannels);
    cab.prepare (sampleRate, samplesPerBlock, numChannels);
    outputGainStage.prepare (sampleRate, samplesPerBlock, numChannels);
    meterTap.reset();

    // Screamer's latency (getLatencySamples()) is only valid once prepare()
    // has run; refresh the reported host latency now that it is.
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

    // 2.5. Floor pedals, in the current chain order. A single relaxed atomic
    // load of the packed order, then a fixed-size loop -- glitch-free,
    // allocation-free reordering (see ChainOrder.h / PluginProcessor.h).
    {
        const auto order = getChainOrder();
        for (auto slot : order)
            processPedalSlot (slot, buffer);
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

void SimpleGuitarAudioProcessor::processPedalSlot (sg::PedalSlot slot, juce::AudioBuffer<float>& buffer) noexcept
{
    switch (slot)
    {
        case sg::PedalSlot::screamer:
            screamer.setEnabled (screamerOnParam->load (std::memory_order_relaxed) >= 0.5f);
            screamer.setDrive (screamerDriveParam->load (std::memory_order_relaxed));
            screamer.setTone (screamerToneParam->load (std::memory_order_relaxed));
            screamer.setLevel (screamerLevelParam->load (std::memory_order_relaxed));
            screamer.process (buffer);
            break;

        case sg::PedalSlot::echoes:
            echoes.setEnabled (echoesOnParam->load (std::memory_order_relaxed) >= 0.5f);
            echoes.setTime (echoesTimeParam->load (std::memory_order_relaxed));
            echoes.setFeedback (echoesFeedbackParam->load (std::memory_order_relaxed));
            echoes.setMix (echoesMixParam->load (std::memory_order_relaxed));
            echoes.process (buffer);
            break;

        case sg::PedalSlot::chamber:
            chamber.setEnabled (chamberOnParam->load (std::memory_order_relaxed) >= 0.5f);
            chamber.setDecay (chamberDecayParam->load (std::memory_order_relaxed));
            chamber.setTone (chamberToneParam->load (std::memory_order_relaxed));
            chamber.setMix (chamberMixParam->load (std::memory_order_relaxed));
            chamber.process (buffer);
            break;
    }
}

void SimpleGuitarAudioProcessor::updateReportedLatency()
{
    // Screamer is the only pedal that adds latency (fixed once prepare() has
    // run); its contribution is order-independent (serial-chain latencies
    // just sum), so this doesn't need to know the current chain order, only
    // whether it's switched on.
    const bool screamerOn = screamerOnParam->load (std::memory_order_relaxed) >= 0.5f;
    const int newLatency = screamerOn ? screamer.getLatencySamples() : 0;

    if (newLatency != getLatencySamples())
        setLatencySamples (newLatency);
}

void SimpleGuitarAudioProcessor::timerCallback()
{
    // Low-rate message-thread poll for screamerOn changes -- see the
    // class-level comment in PluginProcessor.h for why this can't be a
    // parameter listener (JUCE may invoke those from the audio thread for
    // host automation) and must instead be a message-thread refresh that
    // works whether or not an editor/WebviewBridge exists.
    updateReportedLatency();

    // Same reasoning drives dirty-tracking's param-change detection: a
    // low-rate message-thread poll rather than a parameter listener.
    pollForDirtyParamChange();
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

bool SimpleGuitarAudioProcessor::setChainOrder (const sg::PedalOrder& order)
{
    if (! sg::storePedalOrder (chainOrderAtomic, order))
        return false;

    apvts.state.setProperty (chainOrderPropertyId, chainOrderToString (order), nullptr);

    // setChainOrder() is only ever called from a genuine user-driven
    // reorder (WebviewBridge::handleSetChainOrder) -- restoring a persisted
    // order (restoreLoadedPathsFromState()) writes the atomic directly and
    // never calls this, so there's no restore-vs-user ambiguity to guard
    // against here, unlike the nam/ir loaders above.
    presetDirty.store (true, std::memory_order_relaxed);
    return true;
}

const char* SimpleGuitarAudioProcessor::pedalIdForSlot (sg::PedalSlot slot) noexcept
{
    switch (slot)
    {
        case sg::PedalSlot::screamer: return "screamer";
        case sg::PedalSlot::echoes:   return "echoes";
        case sg::PedalSlot::chamber:  return "chamber";
    }
    return "screamer";
}

bool SimpleGuitarAudioProcessor::pedalSlotForId (juce::StringRef id, sg::PedalSlot& outSlot) noexcept
{
    const juce::String idString (id);

    if (idString == "screamer") { outSlot = sg::PedalSlot::screamer; return true; }
    if (idString == "echoes")   { outSlot = sg::PedalSlot::echoes; return true; }
    if (idString == "chamber")  { outSlot = sg::PedalSlot::chamber; return true; }

    return false;
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

    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
    else
        return;

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

    // Chain order: the state tree already carries the persisted value
    // verbatim (it just came in via apvts.replaceState()), so this only
    // needs to update the audio-thread-visible atomic, not re-persist it.
    // An empty/missing/corrupt property leaves the atomic at whatever it
    // already was (the default template order, for a first load).
    const auto chainOrderText = apvts.state.getProperty (chainOrderPropertyId, "").toString();
    sg::PedalOrder restoredOrder {};

    if (chainOrderText.isNotEmpty() && parseChainOrderString (chainOrderText, restoredOrder))
        sg::storePedalOrder (chainOrderAtomic, restoredOrder);

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

    // Same restore path setStateInformation uses (apvts.replaceState() +
    // restoreLoadedPathsFromState()), called directly/synchronously rather
    // than marshaled via MessageManager::callAsync -- loadPreset() is
    // documented message-thread-only (unlike setStateInformation, which the
    // host may call from any thread), so there's no cross-thread hazard to
    // guard against here, and a synchronous restore means the presetsState
    // WebviewBridge sends right after this returns is already accurate.
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
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
