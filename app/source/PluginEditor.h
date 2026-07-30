#pragma once

#include <functional>
#include <optional>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"
#include "WebviewBridge.h"

/**
    Webview-hosted editor: serves the built React UI (ui/dist, zipped and
    embedded as BinaryData at build time -- see app/CMakeLists.txt) through a
    juce::WebBrowserComponent, and wires it to the processor via
    WebviewBridge (see WebviewBridge.h for the exact wire contract, which
    matches ui/src/bridge/*.ts).
*/
class SimpleGuitarAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SimpleGuitarAudioProcessorEditor (SimpleGuitarAudioProcessor&);
    ~SimpleGuitarAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** WebBrowserComponent subclass that keeps navigation confined to our
        embedded resource root, and notifies the bridge once the page (and
        therefore the React app's JS) has actually loaded. */
    struct WebPage final : public juce::WebBrowserComponent
    {
        using juce::WebBrowserComponent::WebBrowserComponent;

        std::function<void()> onFinishedLoading;

        bool pageAboutToLoad (const juce::String& newURL) override;
        void pageFinishedLoading (const juce::String& url) override;
    };

    /** Resource provider serving ui/dist (zipped into BinaryData) at "/",
        "/assets/...", etc. See app/CMakeLists.txt for how the zip is built
        and embedded. */
    static std::optional<juce::WebBrowserComponent::Resource> getUiResource (const juce::String& url);

    SimpleGuitarAudioProcessor& processorRef;

    WebviewBridge bridge;
    WebPage webView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleGuitarAudioProcessorEditor)
};
