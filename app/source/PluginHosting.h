#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PedalSlots.h"
#include "PluginScanCore.h"

/**
    Runtime pieces of third-party plugin hosting (M3): the pedal-slot
    adapter that wraps a live juce::AudioPluginInstance, the out-of-process
    scanner coordinator, and the floating native editor window. The pure,
    directly-testable halves live in PluginScanCore.h (child protocol +
    cache XML) and PluginSlotCore.h (wet/dry blend + state-blob base64).

    Threading follows the codebase-wide rules (see CLAUDE.md and
    PedalSlots.h): everything here except HostedPluginPedal::process()/
    setEnabled()/setParam()/getLatencySamples() is message-thread only; the
    audio-thread entry points are lock-free and allocation-free.
*/
namespace sg
{

//==============================================================================
/** PedalInstance adapter around a live hosted plugin (see PedalSlots.h's
    PedalInstance contract). Constructed only by prepareHostedPluginPedal()
    below, which guarantees the wrapped instance is already bus-configured
    (stereo in/out) and prepared before any audio-thread code can see it.

    P1 (setParam index 0) is the engine-side wet/dry mix: an equal-power dry
    blend around the plugin (see sg::equalPowerWetDry). P2-P4 are unused.
    setEnabled(false) is a bit-exact hard passthrough -- the hosted plugin
    is not run at all while bypassed, matching the "a bypassed pedal
    contributes no latency" contract the latency poll relies on.

    getInstance() is for message-thread callers only (editor window, state
    capture) -- never touch the returned instance from the audio thread
    other than through process(). */
class HostedPluginPedal final : public PedalInstance
{
public:
    void process (juce::AudioBuffer<float>& buffer) noexcept override;
    void setEnabled (bool shouldBeEnabled) noexcept override;
    void setParam (int index, float v01) noexcept override;
    int getLatencySamples() const noexcept override;

    /** Message-thread only. Always non-null. */
    juce::AudioPluginInstance* getInstance() const noexcept { return instance.get(); }

private:
    friend std::unique_ptr<HostedPluginPedal> prepareHostedPluginPedal (std::unique_ptr<juce::AudioPluginInstance>,
                                                                        double, int, int, juce::String&);
    HostedPluginPedal (std::unique_ptr<juce::AudioPluginInstance> preparedInstance, int maxBlockSize, int numChannels);

    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::AudioBuffer<float> dryScratch;
    juce::MidiBuffer midiScratch; // stays empty -- hosted plugins get no MIDI in this milestone
    int preparedChannels = 2;

    // Audio-thread-only state (written/read exclusively from process()'s
    // calling thread via setEnabled/setParam, same pattern as every other
    // PedalInstance adapter).
    bool enabled = true;
    float mix01 = 0.5f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostedPluginPedal)
};

/** Bus-configures (main buses forced to stereo in / stereo out) and
    prepares `instance`, then wraps it in a HostedPluginPedal ready for
    PedalSlotHost::swapSlotInstance(). Returns null with a human-readable
    outError if the plugin can't do stereo-in/stereo-out (the one supported
    layout -- the whole chain is stereo). Message-thread only; may
    allocate. */
std::unique_ptr<HostedPluginPedal> prepareHostedPluginPedal (std::unique_ptr<juce::AudioPluginInstance> instance,
                                                             double sampleRate,
                                                             int maxBlockSize,
                                                             int numChannels,
                                                             juce::String& outError);

//==============================================================================
/** Floating native window hosting a plugin's own editor UI -- the one
    sanctioned native-window exception to the "no OS dialogs/windows" UI
    rule (the plugin's editor is foreign UI; it can't live inside the
    webview). Native title bar, always-on-top so it floats above the app
    window. Closing it (the title-bar close button) invokes onCloseRequest,
    whose owner is expected to destroy this window; destruction tears the
    hosted editor down cleanly (the wrapped editor's own destructor calls
    editorBeingDeleted() on the instance). Message-thread only. */
class PluginEditorWindow final : public juce::DocumentWindow
{
public:
    /** `instance` must outlive this window -- the owner (PluginProcessor)
        guarantees it by closing the window before retiring the instance. */
    PluginEditorWindow (juce::AudioPluginInstance& instance, std::function<void()> onCloseRequestToUse);
    ~PluginEditorWindow() override;

    void closeButtonPressed() override;

private:
    std::function<void()> onCloseRequest;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditorWindow)
};

//==============================================================================
/** Documents/Simple Guitar/plugins.xml. Does not touch the filesystem. */
juce::File getPluginCacheFile();

/** The child-process half of the scanner: scans exactly one candidate
    plugin file with the given format manager's formats (in-process -- this
    IS the process that's allowed to die) and writes the sentinel-wrapped
    result XML (see PluginScanCore.h) to `outFile`, or to stdout if
    `outFile` is File(). Returns the process exit code (0 on success,
    including "no plugins in this file"; nonzero only for unusable
    arguments). Called from the Standalone app's hidden --scan-plugin
    command-line mode (see StandaloneApp.cpp); must run on the message
    thread (VST3 scanning requires it). */
int runPluginScanWorker (const juce::String& pluginPath, const juce::File& outFile);

//==============================================================================
/**
    Parent-side scanner + plugin registry: owns the KnownPluginList (with
    its blacklist), the persisted cache (plugins.xml, see PluginScanCore.h
    for the format), and the scan worker thread.

    Scanning is out-of-process: a KnownPluginList::CustomScanner launches
    the Standalone executable once per candidate file
    (`--scan-plugin <file> --scan-out <tempfile>`), waits with a hard
    timeout, and parses the result file -- a candidate that crashes or
    hangs kills (or is killed in) the CHILD process only, gets blacklisted
    in the KnownPluginList (skipped on every future scan), and is recorded
    by name in the cache's CRASHED list for reporting. Candidates already
    in the list with unchanged file timestamps are skipped
    (KnownPluginList's own timestamp handling via
    PluginDirectoryScanner/scanAndAddFile).

    The scan loop runs on a dedicated worker thread (launching+waiting on
    child processes must not block the message thread); progress/done
    callbacks are marshaled back to the message thread. Everything else
    (registry queries, instance creation, cache save/load) is
    message-thread only.
*/
class PluginScanner final
{
public:
    PluginScanner();
    ~PluginScanner();

    /** Loads the persisted cache (plugins.xml) if present. Message-thread
        only; call once at startup (PluginProcessor's constructor). */
    void loadCacheFromDisk();

    /** Persists the current list/blacklist/folders/crash records to
        plugins.xml. Message-thread only. */
    void saveCacheToDisk() const;

    /** All scanned plugins, sorted by name. Message-thread only. */
    juce::Array<juce::PluginDescription> getPluginsSortedByName() const;

    /** Looks up a scanned plugin by its KnownPluginList identifier string
        (PluginDescription::createIdentifierString() -- the id the bridge
        exposes as PluginEntry.id and persists as slot{N}PluginId). Null if
        unknown. Message-thread only. */
    std::unique_ptr<juce::PluginDescription> findByIdentifier (const juce::String& identifierString) const;

    /** Format manager for createPluginInstanceAsync. Message-thread only. */
    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }

    bool isScanning() const noexcept;

    /** Starts an incremental scan of the standard VST3 locations plus the
        cache's user-added folders, using `workerExecutable` as the
        out-of-process scan worker (the Standalone exe -- the caller
        resolves it; see PluginProcessor::startPluginScan()). Both callbacks
        fire on the message thread; onDone also runs after the cache has
        been saved. Returns false (no scan started, no callbacks) if a scan
        is already running or the worker executable doesn't exist.
        Message-thread only. */
    bool startScan (const juce::File& workerExecutable,
                    std::function<void (int done, int total, juce::String currentName)> onProgress,
                    std::function<void (int totalKnown, juce::StringArray failedNames)> onDone);

    /** Per-candidate child-process timeout. A worker that neither finishes
        nor dies within this window is killed and the candidate treated as
        a crasher. */
    static constexpr int scanChildTimeoutMs = 60'000;

private:
    class ChildProcessScannerImpl;
    class ScanThread;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    // Persisted alongside the list (see PluginScanCore.h). extraScanFolders
    // has no writer yet (the UI wave adds folder management) but is loaded/
    // saved so hand-added entries in plugins.xml already work.
    juce::StringArray extraScanFolders;
    std::vector<CrashedPluginRecord> crashedRecords;

    std::unique_ptr<ScanThread> scanThread;

    JUCE_DECLARE_WEAK_REFERENCEABLE (PluginScanner)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginScanner)
};

} // namespace sg
