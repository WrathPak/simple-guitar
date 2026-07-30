#include "PluginEditor.h"

#include <unordered_map>
#include <vector>

#include <BinaryData.h>

namespace
{
    const juce::Colour backgroundColour (0xff0a0a0c);

    constexpr int defaultWidth = 1180;
    constexpr int defaultHeight = 760;
    constexpr int minWidth = 900;
    constexpr int minHeight = 600;

    //==========================================================================
    const char* mimeTypeForExtension (const juce::String& extension)
    {
        static const std::unordered_map<juce::String, const char*> mimeMap {
            { "htm", "text/html" },
            { "html", "text/html" },
            { "txt", "text/plain" },
            { "jpg", "image/jpeg" },
            { "jpeg", "image/jpeg" },
            { "svg", "image/svg+xml" },
            { "ico", "image/vnd.microsoft.icon" },
            { "json", "application/json" },
            { "png", "image/png" },
            { "css", "text/css" },
            { "map", "application/json" },
            { "js", "text/javascript" },
            { "woff", "font/woff" },
            { "woff2", "font/woff2" },
        };

        if (const auto it = mimeMap.find (extension.toLowerCase()); it != mimeMap.end())
            return it->second;

        return "application/octet-stream";
    }

    juce::String extensionOf (const juce::String& filename)
    {
        return filename.fromLastOccurrenceOf (".", false, false);
    }

    std::vector<std::byte> streamToBytes (juce::InputStream& stream)
    {
        std::vector<std::byte> result ((size_t) stream.getTotalLength());
        stream.setPosition (0);
        [[maybe_unused]] const auto bytesRead = stream.read (result.data(), (int) result.size());
        jassert (bytesRead == (juce::int64) result.size());
        return result;
    }

    /** The zipped ui/dist bundle, embedded as BinaryData by app/CMakeLists.txt
        (juce_add_binary_data on the zip produced from `npm run build`). */
    juce::ZipFile& getUiZipFile()
    {
        static juce::MemoryInputStream zipStream (BinaryData::uidist_zip, (size_t) BinaryData::uidist_zipSize, false);
        static juce::ZipFile zip (zipStream);
        return zip;
    }
}

//==============================================================================
bool SimpleGuitarAudioProcessorEditor::WebPage::pageAboutToLoad (const juce::String& newURL)
{
    // Keep navigation confined to our own resource-provider root -- this is a
    // single-page app with no reason to ever navigate elsewhere.
    return newURL == juce::WebBrowserComponent::getResourceProviderRoot();
}

void SimpleGuitarAudioProcessorEditor::WebPage::pageFinishedLoading (const juce::String&)
{
    if (onFinishedLoading)
        onFinishedLoading();
}

//==============================================================================
std::optional<juce::WebBrowserComponent::Resource> SimpleGuitarAudioProcessorEditor::getUiResource (const juce::String& url)
{
    const auto requested = url == "/" ? juce::String { "index.html" }
                                       : url.fromFirstOccurrenceOf ("/", false, false);

    auto& archive = getUiZipFile();

    if (auto* entry = archive.getEntry (requested))
    {
        std::unique_ptr<juce::InputStream> stream (archive.createStreamForEntry (*entry));

        if (stream != nullptr)
        {
            auto mime = mimeTypeForExtension (extensionOf (entry->filename));
            return juce::WebBrowserComponent::Resource { streamToBytes (*stream), juce::String { mime } };
        }
    }

    return std::nullopt;
}

//==============================================================================
SimpleGuitarAudioProcessorEditor::SimpleGuitarAudioProcessorEditor (SimpleGuitarAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      bridge (p),
      webView (juce::WebBrowserComponent::Options {}
#if JUCE_WINDOWS
                   .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                   .withWinWebView2Options (
                       juce::WebBrowserComponent::Options::WinWebView2 {}
                           .withUserDataFolder (juce::File::getSpecialLocation (juce::File::SpecialLocationType::tempDirectory)))
#endif
                   // Default backend elsewhere (WKWebView on macOS) -- untested on this
                   // Windows-only build machine, but requires no extra wiring: JUCE picks
                   // it automatically when no explicit backend is requested.
                   .withNativeIntegrationEnabled (true)
                   .withEventListener (WebviewBridge::outputGainChannelId, bridge.makeOutputGainListener())
                   .withResourceProvider (&SimpleGuitarAudioProcessorEditor::getUiResource))
{
    webView.onFinishedLoading = [this]
    {
        bridge.attachBrowser (&webView);
    };

    addAndMakeVisible (webView);
    webView.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable (true, true);
    setResizeLimits (minWidth, minHeight, 4000, 4000);
    setSize (defaultWidth, defaultHeight);
}

SimpleGuitarAudioProcessorEditor::~SimpleGuitarAudioProcessorEditor() = default;

void SimpleGuitarAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);
}

void SimpleGuitarAudioProcessorEditor::resized()
{
    webView.setBounds (getLocalBounds());
}
