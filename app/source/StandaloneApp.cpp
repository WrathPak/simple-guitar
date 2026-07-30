// Custom Standalone application for Simple Guitar. Two jobs:
//
//   1. The hidden out-of-process scan-worker mode: launched as
//        "Simple Guitar.exe" --scan-plugin <file> [--scan-out <file>]
//      it scans exactly ONE candidate plugin file in-process (this is the
//      process that's allowed to crash -- see PluginHosting.h) and writes
//      the sentinel-wrapped result XML, then exits without ever creating a
//      window or opening an audio device. The parent app (PluginScanner)
//      launches one of these per candidate.
//
//   2. The normal standalone app, otherwise identical to JUCE's stock
//      StandaloneFilterApp: that class is compiled out when
//      JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 (see
//      juce_audio_plugin_client_Standalone.cpp), so the equivalent behavior
//      is reproduced here on top of the same public
//      StandaloneFilterWindow/StandalonePluginHolder pieces. Ported from
//      JUCE's juce_audio_plugin_client_Standalone.cpp (StandaloneFilterApp)
//      -- same license as the JUCE framework this target already links.

// Module headers first -- juce_StandaloneFilterWindow.h expects the same
// environment JUCE's own Standalone client TU sets up before including it.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "PluginHosting.h"

namespace
{

class SimpleGuitarStandaloneApp final : public juce::JUCEApplication
{
public:
    SimpleGuitarStandaloneApp()
    {
        juce::PropertiesFile::Options options;

        options.applicationName = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config";
       #else
        options.folderName = "";
       #endif

        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        // Hidden scan-worker mode (see file comment). Checked before any
        // window or audio-device work so a scan worker never touches the
        // user's audio setup.
        const auto args = getCommandLineParameterArray();
        const int scanFlagIndex = args.indexOf ("--scan-plugin");

        if (scanFlagIndex >= 0)
        {
            const auto pluginPath = args[scanFlagIndex + 1]; // empty if missing -> worker reports exit code 2
            const int outFlagIndex = args.indexOf ("--scan-out");
            const auto outFile = outFlagIndex >= 0 && args[outFlagIndex + 1].isNotEmpty()
                                     ? juce::File (args[outFlagIndex + 1])
                                     : juce::File();

            setApplicationReturnValue (sg::runPluginScanWorker (pluginPath, outFile));
            quit();
            return;
        }

        mainWindow.reset (createWindow());

        if (mainWindow != nullptr)
        {
           #if JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE
            juce::Desktop::getInstance().setKioskModeComponent (mainWindow.get(), false);
           #endif

            mainWindow->setVisible (true);
        }
        else
        {
            pluginHolder = createPluginHolder();
        }
    }

    void shutdown() override
    {
        pluginHolder = nullptr;
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (pluginHolder != nullptr)
            pluginHolder->savePluginState();

        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []
            {
                if (auto* app = JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    juce::StandaloneFilterWindow* createWindow()
    {
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            // No displays are available, so no window will be created.
            jassertfalse;
            return nullptr;
        }

        return new juce::StandaloneFilterWindow (getApplicationName(),
                                                 juce::LookAndFeel::getDefaultLookAndFeel()
                                                     .findColour (juce::ResizableWindow::backgroundColourId),
                                                 createPluginHolder());
    }

    std::unique_ptr<juce::StandalonePluginHolder> createPluginHolder()
    {
        constexpr auto autoOpenMidiDevices =
       #if (JUCE_ANDROID || JUCE_IOS) && ! JUCE_DONT_AUTO_OPEN_MIDI_DEVICES_ON_MOBILE
            true;
       #else
            false;
       #endif

       #ifdef JucePlugin_PreferredChannelConfigurations
        constexpr juce::StandalonePluginHolder::PluginInOuts channels[] { JucePlugin_PreferredChannelConfigurations };
        const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig (channels, juce::numElementsInArray (channels));
       #else
        const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig;
       #endif

        return std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(),
                                                               false,
                                                               juce::String {},
                                                               nullptr,
                                                               channelConfig,
                                                               autoOpenMidiDevices);
    }

    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
    std::unique_ptr<juce::StandalonePluginHolder> pluginHolder;
};

} // namespace

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new SimpleGuitarStandaloneApp();
}
