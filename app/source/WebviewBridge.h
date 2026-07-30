#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

/**
    Hand-rolled bridge between the React UI (loaded into a WebBrowserComponent)
    and the processor's APVTS "outputGain" parameter + MeterTap.

    This conforms exactly to the wire contract documented in
    ui/src/bridge/sliderState.ts and ui/src/bridge/meterFrame.ts, which is a
    small hand-rolled protocol built directly on the two primitives JUCE
    injects when native integration is enabled (window.__JUCE__.backend.
    addEventListener / emitEvent) -- NOT JUCE's own WebSliderRelay wire format
    (which uses an "eventType" key); ours uses "type", matching the UI:

      JS -> native, channel "outputGain":
        { type: "valueChanged", value: <0..1 normalized> }
        { type: "gestureStart" }
        { type: "gestureEnd" }
      native -> JS, channel "outputGain":
        { type: "valueChanged", value: <0..1 normalized> }
          -- pushed once right after the page finishes loading, and again
             whenever the host changes the parameter without the UI having
             caused it (automation, preset/state restore).
      native -> JS, channel "meterFrame" (~30Hz, via
        WebBrowserComponent::emitEventIfBrowserIsVisible so nothing is sent
        while the editor isn't visible):
        { inPeakDb: number, outPeakDb: number }

    Meter note: the processor only taps a single post-gain (post sg::Gain)
    stereo peak meter (see PluginProcessor::processBlock: input -> gain ->
    meterTap -> output). There is no separate pre-gain tap. outPeakDb is
    therefore the real measured post-gain peak; inPeakDb is derived from it
    by undoing the currently-applied gain in dB (in = out - gainDb), which is
    exact for a pure gain stage modulo the ~20ms smoothing ramp in sg::Gain.
    This is a known M0 simplification -- see the handoff report.
*/
class WebviewBridge final : private juce::Timer
{
public:
    explicit WebviewBridge (SimpleGuitarAudioProcessor& processorToUse);
    ~WebviewBridge() override;

    /** The relay/channel names, shared with ui/src/bridge/*.ts. */
    static constexpr const char* outputGainChannelId = "outputGain";
    static constexpr const char* meterFrameChannelId = "meterFrame";

    /** Build the event listener to pass to
        WebBrowserComponent::Options::withEventListener (outputGainChannelId, ...). */
    juce::WebBrowserComponent::NativeEventListener makeOutputGainListener();

    /** Call once the WebBrowserComponent exists (e.g. from pageFinishedLoading()).
        Pushes the current parameter value to the UI and starts the ~30Hz
        meter/automation-echo timer. Safe to call more than once (e.g. on
        page reload); each call re-pushes the initial value. */
    void attachBrowser (juce::WebBrowserComponent* browserToUse);

private:
    void handleOutputGainEvent (const juce::var& event);
    void pushCurrentValueToUi();
    void timerCallback() override;

    SimpleGuitarAudioProcessor& processor;
    juce::RangedAudioParameter& outputGainParam;
    juce::WebBrowserComponent* browser = nullptr;

    // Last normalized value we know the UI has (either because we just sent
    // it, or the UI just told us). Used to suppress redundant automation-echo
    // events and avoid a feedback loop between the two directions.
    std::atomic<float> lastKnownUiValue { -1.0f };

    static constexpr float meterFloorDb = -60.0f;
    static constexpr int meterTimerHz = 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebviewBridge)
};
