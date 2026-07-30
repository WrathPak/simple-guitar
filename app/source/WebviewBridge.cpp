#include "WebviewBridge.h"

#include "RigLibrary.h"

namespace
{
    constexpr const char* typeKey = "type";
    constexpr const char* valueKey = "value";
    constexpr const char* pathKey = "path";
    constexpr const char* valueChangedType = "valueChanged";
    constexpr const char* gestureStartType = "gestureStart";
    constexpr const char* gestureEndType = "gestureEnd";
    constexpr const char* inPeakDbKey = "inPeakDb";
    constexpr const char* outPeakDbKey = "outPeakDb";

    juce::var libraryEntriesToVar (const std::vector<sg::LibraryEntry>& entries)
    {
        juce::Array<juce::var> array;

        for (const auto& entry : entries)
        {
            juce::DynamicObject::Ptr entryObj (new juce::DynamicObject());
            entryObj->setProperty ("name", entry.name);
            entryObj->setProperty ("path", entry.path);
            array.add (juce::var (entryObj.get()));
        }

        return juce::var (array);
    }
}

WebviewBridge::WebviewBridge (SimpleGuitarAudioProcessor& processorToUse)
    : processor (processorToUse)
{
    for (std::size_t i = 0; i < params.size(); ++i)
    {
        params[i] = processor.apvts.getParameter (SimpleGuitarAudioProcessor::allParamIds[i]);
        lastKnownUiValue[i].store (-1.0f, std::memory_order_relaxed);
    }
}

WebviewBridge::~WebviewBridge()
{
    stopTimer();
}

juce::WebBrowserComponent::Options WebviewBridge::attachListenersTo (juce::WebBrowserComponent::Options opts)
{
    for (int i = 0; i < SimpleGuitarAudioProcessor::numParams; ++i)
    {
        opts = opts.withEventListener (SimpleGuitarAudioProcessor::allParamIds[(std::size_t) i],
                                        [this, i] (const juce::var& event) { handleParamEvent (i, event); });
    }

    opts = opts.withEventListener (loadNamModelChannelId, [this] (const juce::var& event) { handleLoadNamModel (event); });
    opts = opts.withEventListener (loadIrChannelId, [this] (const juce::var& event) { handleLoadIr (event); });
    opts = opts.withEventListener (requestRigStateChannelId, [this] (const juce::var& event) { handleRequestRigState (event); });
    opts = opts.withEventListener (setChainOrderChannelId, [this] (const juce::var& event) { handleSetChainOrder (event); });
    opts = opts.withEventListener (loadPresetChannelId, [this] (const juce::var& event) { handleLoadPreset (event); });
    opts = opts.withEventListener (savePresetAsChannelId, [this] (const juce::var& event) { handleSavePresetAs (event); });
    opts = opts.withEventListener (saveCurrentPresetChannelId, [this] (const juce::var& event) { handleSaveCurrentPreset (event); });
    opts = opts.withEventListener (nextPresetChannelId, [this] (const juce::var& event) { handleNextPreset (event); });
    opts = opts.withEventListener (prevPresetChannelId, [this] (const juce::var& event) { handlePrevPreset (event); });
    opts = opts.withEventListener (requestPresetsChannelId, [this] (const juce::var& event) { handleRequestPresets (event); });

    return opts;
}

void WebviewBridge::handleParamEvent (int paramIndex, const juce::var& event)
{
    auto* obj = event.getDynamicObject();
    if (obj == nullptr)
        return;

    auto* param = params[(std::size_t) paramIndex];
    if (param == nullptr)
        return;

    const auto type = obj->getProperty (typeKey).toString();

    if (type == valueChangedType)
    {
        const auto valueVar = obj->getProperty (valueKey);
        if (! (valueVar.isDouble() || valueVar.isInt() || valueVar.isInt64()))
            return;

        const auto normalized = juce::jlimit (0.0f, 1.0f, (float) valueVar);
        param->setValueNotifyingHost (normalized);

        // We already know the UI has this value (it just told us) -- don't
        // echo it straight back on the next timer tick.
        lastKnownUiValue[(std::size_t) paramIndex].store (normalized, std::memory_order_relaxed);
    }
    else if (type == gestureStartType)
    {
        param->beginChangeGesture();
    }
    else if (type == gestureEndType)
    {
        param->endChangeGesture();
    }
}

void WebviewBridge::handleLoadNamModel (const juce::var& event)
{
    auto* obj = event.getDynamicObject();
    const auto path = obj != nullptr ? obj->getProperty (pathKey).toString() : juce::String();

    if (path.isEmpty())
    {
        sendLoadResult ("nam", false, "no path given");
        sendRigState();
        return;
    }

    const juce::File file (path);

    if (! sg::isInsideFolder (file, sg::getModelsLibraryFolder()))
    {
        sendLoadResult ("nam", false, "path is outside the managed Models library");
        sendRigState();
        return;
    }

    juce::WeakReference<WebviewBridge> safeThis (this);

    processor.requestLoadNamModel (path, [safeThis] (bool ok, juce::String errorOrName)
    {
        if (auto* self = safeThis.get())
        {
            self->sendLoadResult ("nam", ok, errorOrName);
            self->sendRigState();
        }
    });
}

void WebviewBridge::handleLoadIr (const juce::var& event)
{
    auto* obj = event.getDynamicObject();
    const auto path = obj != nullptr ? obj->getProperty (pathKey).toString() : juce::String();

    if (path.isEmpty())
    {
        sendLoadResult ("ir", false, "no path given");
        sendRigState();
        return;
    }

    const juce::File file (path);

    if (! sg::isInsideFolder (file, sg::getIrsLibraryFolder()))
    {
        sendLoadResult ("ir", false, "path is outside the managed IRs library");
        sendRigState();
        return;
    }

    juce::WeakReference<WebviewBridge> safeThis (this);

    processor.requestLoadIr (path, [safeThis] (bool ok, juce::String errorOrName)
    {
        if (auto* self = safeThis.get())
        {
            self->sendLoadResult ("ir", ok, errorOrName);
            self->sendRigState();
        }
    });
}

void WebviewBridge::handleRequestRigState (const juce::var&)
{
    sendRigState();
}

void WebviewBridge::handleSetChainOrder (const juce::var& event)
{
    auto* obj = event.getDynamicObject();
    if (obj == nullptr)
        return;

    const auto orderVar = obj->getProperty ("order");
    if (! orderVar.isArray())
        return;

    const auto* orderArray = orderVar.getArray();
    if (orderArray == nullptr || orderArray->size() != sg::numPedalSlots)
        return; // malformed -- silently rejected, per the wire contract.

    sg::PedalOrder order {};
    for (int i = 0; i < sg::numPedalSlots; ++i)
    {
        if (! SimpleGuitarAudioProcessor::pedalSlotForId ((*orderArray)[i].toString(), order[(std::size_t) i]))
            return; // unknown pedal id -- silently rejected.
    }

    if (! processor.setChainOrder (order))
        return; // not a permutation (e.g. a duplicate) -- silently rejected.

    sendRigState();
}

void WebviewBridge::handleLoadPreset (const juce::var& event)
{
    auto* obj = event.getDynamicObject();
    const auto path = obj != nullptr ? obj->getProperty (pathKey).toString() : juce::String();

    juce::String error;
    const bool ok = path.isNotEmpty() && processor.loadPreset (path, error);

    if (! ok && error.isEmpty())
        error = "no path given";

    sendPresetResult (ok, ok ? "loaded" : error);
    sendPresetsState();
}

void WebviewBridge::handleSavePresetAs (const juce::var& event)
{
    auto* obj = event.getDynamicObject();
    const auto name = obj != nullptr ? obj->getProperty ("name").toString() : juce::String();

    juce::String error;
    const bool ok = processor.savePresetAs (name, error);

    sendPresetResult (ok, ok ? "saved" : error);
    sendPresetsState();
}

void WebviewBridge::handleSaveCurrentPreset (const juce::var&)
{
    juce::String error;
    const bool ok = processor.saveCurrentPreset (error);

    sendPresetResult (ok, ok ? "saved" : error);
    sendPresetsState();
}

void WebviewBridge::handleNextPreset (const juce::var&)
{
    juce::String error;
    processor.nextPreset (error); // best-effort; no inline failure surface for this control.
    sendPresetsState();
}

void WebviewBridge::handlePrevPreset (const juce::var&)
{
    juce::String error;
    processor.prevPreset (error);
    sendPresetsState();
}

void WebviewBridge::handleRequestPresets (const juce::var&)
{
    sendPresetsState();
}

void WebviewBridge::attachBrowser (juce::WebBrowserComponent* browserToUse)
{
    browser = browserToUse;

    if (browser == nullptr)
        return;

    for (int i = 0; i < SimpleGuitarAudioProcessor::numParams; ++i)
        pushParamValueToUi (i);

    sendRigState();
    sendPresetsState();

    if (! isTimerRunning())
        startTimerHz (meterTimerHz);
}

void WebviewBridge::pushParamValueToUi (int paramIndex)
{
    if (browser == nullptr)
        return;

    auto* param = params[(std::size_t) paramIndex];
    if (param == nullptr)
        return;

    const auto normalized = param->getValue();
    lastKnownUiValue[(std::size_t) paramIndex].store (normalized, std::memory_order_relaxed);

    juce::DynamicObject::Ptr obj (new juce::DynamicObject());
    obj->setProperty (typeKey, valueChangedType);
    obj->setProperty (valueKey, normalized);
    browser->emitEventIfBrowserIsVisible (SimpleGuitarAudioProcessor::allParamIds[(std::size_t) paramIndex], obj.get());
}

void WebviewBridge::sendRigState()
{
    if (browser == nullptr)
        return;

    const auto library = processor.scanRigLibrary();

    juce::DynamicObject::Ptr libraryObj (new juce::DynamicObject());
    libraryObj->setProperty ("models", libraryEntriesToVar (library.models));
    libraryObj->setProperty ("irs", libraryEntriesToVar (library.irs));

    auto& nam = processor.getNam();
    auto& cab = processor.getCab();

    juce::Array<juce::var> chainOrderArray;
    for (auto slot : processor.getChainOrder())
        chainOrderArray.add (juce::String (SimpleGuitarAudioProcessor::pedalIdForSlot (slot)));

    juce::DynamicObject::Ptr obj (new juce::DynamicObject());
    obj->setProperty (typeKey, "rigState");
    obj->setProperty ("schemaVersion", schemaVersion);
    obj->setProperty ("namModelName", nam.isLoaded() ? juce::var (nam.getModelName()) : juce::var());
    obj->setProperty ("namModelSampleRate", nam.getModelSampleRate());
    obj->setProperty ("irName", cab.isLoaded() ? juce::var (cab.getIrName()) : juce::var());
    obj->setProperty ("chainOrder", juce::var (chainOrderArray));
    obj->setProperty ("library", juce::var (libraryObj.get()));

    browser->emitEventIfBrowserIsVisible (rigStateChannelId, obj.get());
}

void WebviewBridge::sendLoadResult (const char* kind, bool ok, const juce::String& message)
{
    if (browser == nullptr)
        return;

    juce::DynamicObject::Ptr obj (new juce::DynamicObject());
    obj->setProperty (typeKey, "loadResult");
    obj->setProperty ("kind", kind);
    obj->setProperty ("ok", ok);
    obj->setProperty ("message", message);

    browser->emitEventIfBrowserIsVisible (loadResultChannelId, obj.get());
}

void WebviewBridge::sendPresetsState()
{
    if (browser == nullptr)
        return;

    const auto presets = processor.listPresets();
    juce::Array<juce::var> presetsArray;
    for (const auto& entry : presets)
    {
        juce::DynamicObject::Ptr entryObj (new juce::DynamicObject());
        entryObj->setProperty ("name", entry.name);
        entryObj->setProperty ("path", entry.path);
        presetsArray.add (juce::var (entryObj.get()));
    }

    juce::var currentVar; // stays void -- serializes as JSON null -- if there's no current preset.
    if (const auto current = processor.getCurrentPreset())
    {
        juce::DynamicObject::Ptr currentObj (new juce::DynamicObject());
        currentObj->setProperty ("name", current->name);
        currentObj->setProperty ("path", current->path);
        currentVar = juce::var (currentObj.get());
    }

    juce::DynamicObject::Ptr obj (new juce::DynamicObject());
    obj->setProperty (typeKey, "presetsState");
    obj->setProperty ("schemaVersion", schemaVersion);
    obj->setProperty ("current", currentVar);
    obj->setProperty ("dirty", processor.isPresetDirty());
    obj->setProperty ("presets", juce::var (presetsArray));

    browser->emitEventIfBrowserIsVisible (presetsStateChannelId, obj.get());

    // Keep the dirty-flip throttle (see timerCallback()) in sync with every
    // emission, whatever triggered it -- not just the ones the throttle
    // itself decides to send.
    lastKnownPresetDirty = processor.isPresetDirty();
    lastPresetDirtyEmitMs = juce::Time::getMillisecondCounter();
}

void WebviewBridge::sendPresetResult (bool ok, const juce::String& message)
{
    if (browser == nullptr)
        return;

    juce::DynamicObject::Ptr obj (new juce::DynamicObject());
    obj->setProperty (typeKey, "presetResult");
    obj->setProperty ("ok", ok);
    obj->setProperty ("message", message);

    browser->emitEventIfBrowserIsVisible (presetResultChannelId, obj.get());
}

void WebviewBridge::timerCallback()
{
    if (browser == nullptr)
        return;

    // --- Automation echo: if the host (automation, preset/state restore)
    // changed a param without the UI causing it, push the new value.
    for (int i = 0; i < SimpleGuitarAudioProcessor::numParams; ++i)
    {
        auto* param = params[(std::size_t) i];
        if (param == nullptr)
            continue;

        const auto currentNormalized = param->getValue();
        const auto lastKnown = lastKnownUiValue[(std::size_t) i].load (std::memory_order_relaxed);

        if (! juce::approximatelyEqual (currentNormalized, lastKnown))
        {
            lastKnownUiValue[(std::size_t) i].store (currentNormalized, std::memory_order_relaxed);

            juce::DynamicObject::Ptr obj (new juce::DynamicObject());
            obj->setProperty (typeKey, valueChangedType);
            obj->setProperty (valueKey, currentNormalized);
            browser->emitEventIfBrowserIsVisible (SimpleGuitarAudioProcessor::allParamIds[(std::size_t) i], obj.get());
        }
    }

    // --- Preset dirty-flip: sendPresetsState() is already called directly
    // after every explicit preset op (see the handle* methods above); this
    // only needs to catch a *param* change flipping dirty false->true (or,
    // in principle, some other path clearing it) in between those, at a
    // throttled rate (~4/s max, see presetDirtyEmitMinIntervalMs) so a fast
    // knob drag can't flood the bridge with presetsState pushes.
    {
        const bool currentDirty = processor.isPresetDirty();
        const auto now = juce::Time::getMillisecondCounter();

        if (currentDirty != lastKnownPresetDirty && (now - lastPresetDirtyEmitMs) >= presetDirtyEmitMinIntervalMs)
            sendPresetsState(); // also refreshes lastKnownPresetDirty/lastPresetDirtyEmitMs.
    }

    // --- Meter frame.
    // The processor taps a single post-chain (post output-gain) stereo peak
    // meter -- same single-tap design as M0, now sitting after a much longer
    // chain (gate/nam/eq/cab in between). outPeakDb is the real measured
    // post-chain peak. inPeakDb is *not* the true guitar input level (that
    // would need a second tap ahead of the gate, out of scope for this
    // milestone's engine work) -- it's derived by undoing only the final
    // output-gain stage in the linear domain, same known-simplification
    // approach as M0's passthrough meter, just measuring "pre output-gain"
    // rather than "pre the only gain stage". Exact for that one stage modulo
    // the ~20ms smoothing ramp in sg::Gain; still not a pre-gate/pre-nam
    // input reading.
    auto& meterTap = processor.getMeterTap();
    const auto peakLinearPostChain = juce::jmax (meterTap.getPeak (0), meterTap.getPeak (1));
    const auto outPeakDb = juce::Decibels::gainToDecibels (peakLinearPostChain, meterFloorDb);

    const auto currentOutputGainDb = processor.apvts.getRawParameterValue (SimpleGuitarAudioProcessor::outputGainParamId)
                                          ->load (std::memory_order_relaxed);
    const auto outputGainLinear = juce::Decibels::decibelsToGain (currentOutputGainDb);
    const auto peakLinearPreOutputGain = outputGainLinear > 0.0f ? peakLinearPostChain / outputGainLinear : 0.0f;
    const auto inPeakDb = juce::Decibels::gainToDecibels (peakLinearPreOutputGain, meterFloorDb);

    juce::DynamicObject::Ptr frame (new juce::DynamicObject());
    frame->setProperty (inPeakDbKey, inPeakDb);
    frame->setProperty (outPeakDbKey, outPeakDb);
    browser->emitEventIfBrowserIsVisible (meterFrameChannelId, frame.get());
}
