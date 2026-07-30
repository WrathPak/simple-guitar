#include "WebviewBridge.h"

namespace
{
    constexpr const char* typeKey = "type";
    constexpr const char* valueKey = "value";
    constexpr const char* valueChangedType = "valueChanged";
    constexpr const char* gestureStartType = "gestureStart";
    constexpr const char* gestureEndType = "gestureEnd";
    constexpr const char* inPeakDbKey = "inPeakDb";
    constexpr const char* outPeakDbKey = "outPeakDb";
}

WebviewBridge::WebviewBridge (SimpleGuitarAudioProcessor& processorToUse)
    : processor (processorToUse),
      outputGainParam (*processor.apvts.getParameter (SimpleGuitarAudioProcessor::outputGainParamId))
{
}

WebviewBridge::~WebviewBridge()
{
    stopTimer();
}

juce::WebBrowserComponent::NativeEventListener WebviewBridge::makeOutputGainListener()
{
    return [this] (const juce::var& event)
    {
        handleOutputGainEvent (event);
    };
}

void WebviewBridge::handleOutputGainEvent (const juce::var& event)
{
    auto* obj = event.getDynamicObject();
    if (obj == nullptr)
        return;

    const auto type = obj->getProperty (typeKey).toString();

    if (type == valueChangedType)
    {
        const auto valueVar = obj->getProperty (valueKey);
        if (! (valueVar.isDouble() || valueVar.isInt() || valueVar.isInt64()))
            return;

        const auto normalized = juce::jlimit (0.0f, 1.0f, (float) valueVar);
        outputGainParam.setValueNotifyingHost (normalized);

        // We already know the UI has this value (it just told us) -- don't
        // echo it straight back on the next timer tick.
        lastKnownUiValue.store (normalized, std::memory_order_relaxed);
    }
    else if (type == gestureStartType)
    {
        outputGainParam.beginChangeGesture();
    }
    else if (type == gestureEndType)
    {
        outputGainParam.endChangeGesture();
    }
}

void WebviewBridge::attachBrowser (juce::WebBrowserComponent* browserToUse)
{
    browser = browserToUse;

    if (browser == nullptr)
        return;

    pushCurrentValueToUi();

    if (! isTimerRunning())
        startTimerHz (meterTimerHz);
}

void WebviewBridge::pushCurrentValueToUi()
{
    if (browser == nullptr)
        return;

    const auto normalized = outputGainParam.getValue();
    lastKnownUiValue.store (normalized, std::memory_order_relaxed);

    juce::DynamicObject::Ptr obj (new juce::DynamicObject());
    obj->setProperty (typeKey, valueChangedType);
    obj->setProperty (valueKey, normalized);
    browser->emitEventIfBrowserIsVisible (outputGainChannelId, obj.get());
}

void WebviewBridge::timerCallback()
{
    if (browser == nullptr)
        return;

    // --- Automation echo: if the host (automation, preset/state restore)
    // changed the parameter without the UI causing it, push the new value.
    const auto currentNormalized = outputGainParam.getValue();
    const auto lastKnown = lastKnownUiValue.load (std::memory_order_relaxed);

    if (! juce::approximatelyEqual (currentNormalized, lastKnown))
    {
        lastKnownUiValue.store (currentNormalized, std::memory_order_relaxed);

        juce::DynamicObject::Ptr obj (new juce::DynamicObject());
        obj->setProperty (typeKey, valueChangedType);
        obj->setProperty (valueKey, currentNormalized);
        browser->emitEventIfBrowserIsVisible (outputGainChannelId, obj.get());
    }

    // --- Meter frame.
    auto& meterTap = processor.getMeterTap();
    const auto peakLinearPostGain = juce::jmax (meterTap.getPeak (0), meterTap.getPeak (1));
    const auto outPeakDb = juce::Decibels::gainToDecibels (peakLinearPostGain, meterFloorDb);

    // The processor only taps a single post-gain meter (see the class
    // comment in WebviewBridge.h); derive the pre-gain peak by undoing the
    // currently-applied gain in the *linear* domain first, then converting
    // to dB with its own independent floor. (Subtracting gainDb from the
    // already-floored outPeakDb would double up the -60dB floor whenever
    // outPeakDb itself was clamped, e.g. a quiet post-gain signal under
    // heavy negative gain -- this ordering avoids that.) Exact for a pure
    // gain stage modulo the ~20ms smoothing ramp in sg::Gain.
    const auto currentGainDb = processor.apvts.getRawParameterValue (SimpleGuitarAudioProcessor::outputGainParamId)
                                   ->load (std::memory_order_relaxed);
    const auto gainLinear = juce::Decibels::decibelsToGain (currentGainDb);
    const auto peakLinearPreGain = gainLinear > 0.0f ? peakLinearPostGain / gainLinear : 0.0f;
    const auto inPeakDb = juce::Decibels::gainToDecibels (peakLinearPreGain, meterFloorDb);

    juce::DynamicObject::Ptr frame (new juce::DynamicObject());
    frame->setProperty (inPeakDbKey, inPeakDb);
    frame->setProperty (outPeakDbKey, outPeakDb);
    browser->emitEventIfBrowserIsVisible (meterFrameChannelId, frame.get());
}
