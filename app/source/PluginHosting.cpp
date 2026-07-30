#include "PluginHosting.h"

#include <algorithm>
#include <cstdio>

#include <juce_events/juce_events.h>

#include "PluginSlotCore.h"

namespace sg
{

//==============================================================================
// HostedPluginPedal

HostedPluginPedal::HostedPluginPedal (std::unique_ptr<juce::AudioPluginInstance> preparedInstance,
                                      int maxBlockSize, int numChannels)
    : instance (std::move (preparedInstance)),
      preparedChannels (numChannels)
{
    dryScratch.setSize (numChannels, maxBlockSize);
}

void HostedPluginPedal::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (! enabled)
        return; // hard bypass: bit-exact passthrough, hosted plugin not run (see header)

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // The instance was bus-configured/prepared for exactly preparedChannels
    // -- a mismatched buffer (shouldn't happen; the whole chain is stereo)
    // falls back to passthrough rather than feeding the plugin a layout it
    // wasn't prepared for.
    if (numChannels != preparedChannels || numSamples > dryScratch.getNumSamples())
        return;

    // setSize with avoidReallocating=true never allocates here (capacity was
    // reserved at construction) -- same pattern as PedalSlotHost's scratch
    // buffers.
    dryScratch.setSize (numChannels, numSamples, false, false, true);
    for (int ch = 0; ch < numChannels; ++ch)
        dryScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    instance->processBlock (buffer, midiScratch);
    midiScratch.clear(); // discard anything the plugin produced -- no MIDI routing in this milestone

    blendWetDryInPlace (buffer, dryScratch, mix01);
}

void HostedPluginPedal::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled = shouldBeEnabled;
}

void HostedPluginPedal::setParam (int index, float v01) noexcept
{
    if (index == 0)
        mix01 = v01;
    // P2-P4 unused for hosted-plugin slots.
}

int HostedPluginPedal::getLatencySamples() const noexcept
{
    return instance->getLatencySamples();
}

std::unique_ptr<HostedPluginPedal> prepareHostedPluginPedal (std::unique_ptr<juce::AudioPluginInstance> instance,
                                                             double sampleRate,
                                                             int maxBlockSize,
                                                             int numChannels,
                                                             juce::String& outError)
{
    if (instance == nullptr)
    {
        outError = "no plugin instance";
        return nullptr;
    }

    if (numChannels != 2)
    {
        outError = "the pedal chain is stereo only";
        return nullptr;
    }

    if (instance->getBusCount (true) < 1 || instance->getBusCount (false) < 1)
    {
        outError = "plugin has no audio input or output";
        return nullptr;
    }

    // Force the main buses to stereo and try to disable every auxiliary bus
    // (sidechains etc. -- nothing feeds them here). If the plugin refuses
    // the aux-disabled layout, retry with the aux buses left at their
    // defaults, but then the TOTAL channel counts must still come out
    // 2-in/2-out (the audio thread hands the instance exactly one stereo
    // buffer, so any extra active channel would be fed garbage).
    auto layout = instance->getBusesLayout();
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();

    auto auxDisabled = layout;
    for (int i = 1; i < auxDisabled.inputBuses.size(); ++i)
        auxDisabled.inputBuses.getReference (i) = juce::AudioChannelSet::disabled();
    for (int i = 1; i < auxDisabled.outputBuses.size(); ++i)
        auxDisabled.outputBuses.getReference (i) = juce::AudioChannelSet::disabled();

    if (! instance->setBusesLayout (auxDisabled) && ! instance->setBusesLayout (layout))
    {
        outError = "plugin can't run as stereo in / stereo out";
        return nullptr;
    }

    if (instance->getTotalNumInputChannels() != 2 || instance->getTotalNumOutputChannels() != 2)
    {
        outError = "plugin can't run as stereo in / stereo out";
        return nullptr;
    }

    instance->setNonRealtime (false);
    instance->setRateAndBufferSizeDetails (sampleRate, maxBlockSize);
    instance->prepareToPlay (sampleRate, maxBlockSize);

    return std::unique_ptr<HostedPluginPedal> (new HostedPluginPedal (std::move (instance), maxBlockSize, numChannels));
}

//==============================================================================
// PluginEditorWindow

PluginEditorWindow::PluginEditorWindow (juce::AudioPluginInstance& instance, std::function<void()> onCloseRequestToUse)
    : juce::DocumentWindow (instance.getName(),
                            juce::Colours::black,
                            juce::DocumentWindow::closeButton),
      onCloseRequest (std::move (onCloseRequestToUse))
{
    setUsingNativeTitleBar (true);
    setAlwaysOnTop (true);

    // Some plugins have no UI at all -- fall back to JUCE's generic
    // parameter editor so the window is never just an empty frame.
    juce::AudioProcessorEditor* editor = instance.createEditorAndMakeActive();
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (instance);

    // Owned: destroying this window destroys the editor, whose own
    // destructor calls editorBeingDeleted() on the instance (see the hosted
    // editor wrappers in juce_audio_processors), so teardown order is safe
    // as long as the window dies before the instance does.
    setContentOwned (editor, true);

    centreWithSize (getWidth(), getHeight());
    setVisible (true);
    toFront (true);
}

PluginEditorWindow::~PluginEditorWindow()
{
    clearContentComponent();
}

void PluginEditorWindow::closeButtonPressed()
{
    if (onCloseRequest)
        onCloseRequest(); // the owner destroys this window
}

//==============================================================================
// Cache file + scan worker (child-process side)

juce::File getPluginCacheFile()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Simple Guitar")
        .getChildFile ("plugins.xml");
}

int runPluginScanWorker (const juce::String& pluginPath, const juce::File& outFile)
{
    if (pluginPath.isEmpty())
        return 2;

    juce::AudioPluginFormatManager formatManager;
    juce::addDefaultFormatsToManager (formatManager);

    juce::OwnedArray<juce::PluginDescription> found;

    for (auto* format : formatManager.getFormats())
        if (format->fileMightContainThisPluginType (pluginPath))
            format->findAllTypesForFile (found, pluginPath); // may crash/hang -- that's the point of being a child process

    juce::XmlElement resultXml ("SCANRESULT");
    for (const auto* description : found)
        resultXml.addChildElement (description->createXml().release());

    const auto wrapped = wrapScanResultXml (resultXml.toString());

    if (outFile == juce::File())
    {
        std::fputs (wrapped.toRawUTF8(), stdout);
        std::fflush (stdout);
    }
    else if (! outFile.replaceWithText (wrapped))
    {
        return 3;
    }

    return 0;
}

//==============================================================================
// PluginScanner

/** The out-of-process CustomScanner (see PluginHosting.h's class comment).
    findPluginTypesFor() runs on the scan thread, synchronously inside
    KnownPluginList::scanAndAddFile(); returning false blacklists the
    candidate in the KnownPluginList. */
class PluginScanner::ChildProcessScannerImpl final : public juce::KnownPluginList::CustomScanner
{
public:
    ChildProcessScannerImpl (const juce::File& workerExecutableToUse,
                             std::function<bool()> shouldAbortToUse,
                             std::function<void (CrashedPluginRecord)> onCrashedToUse)
        : workerExecutable (workerExecutableToUse),
          shouldAbort (std::move (shouldAbortToUse)),
          onCrashed (std::move (onCrashedToUse))
    {
    }

    bool findPluginTypesFor (juce::AudioPluginFormat&,
                             juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override
    {
        const auto crashed = [&] (const juce::String&)
        {
            if (onCrashed)
                onCrashed ({ juce::File (fileOrIdentifier).getFileNameWithoutExtension(), fileOrIdentifier });
            return false; // -> KnownPluginList blacklists the file
        };

        const auto resultFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getNonexistentChildFile ("sg-scan", ".xml");

        juce::ChildProcess child;
        const juce::StringArray args { workerExecutable.getFullPathName(),
                                       "--scan-plugin", fileOrIdentifier,
                                       "--scan-out", resultFile.getFullPathName() };

        // No output pipes (flags 0): the result travels through the temp
        // file, so a plugin spewing megabytes to stdout can't deadlock a
        // full pipe, and a hung child is simply killed on timeout below.
        if (! child.start (args, 0))
            return true; // couldn't launch the worker at all -- not the plugin's fault; skip without blacklisting

        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) PluginScanner::scanChildTimeoutMs;
        bool finished = false;

        while (! finished)
        {
            finished = child.waitForProcessToFinish (200);

            if (finished)
                break;

            if ((shouldAbort != nullptr && shouldAbort()) || juce::Time::getMillisecondCounter() >= deadline)
            {
                child.kill();

                resultFile.deleteFile();

                if (shouldAbort != nullptr && shouldAbort())
                    return true; // scan aborted -- don't blacklist an innocent candidate

                return crashed ("timed out");
            }
        }

        const auto output = resultFile.loadFileAsString();
        resultFile.deleteFile();

        const auto xml = extractScanResultXml (output);

        if (xml == nullptr || ! xml->hasTagName ("SCANRESULT"))
            return crashed ("no result"); // child died before writing a well-formed result

        for (const auto* child_element : xml->getChildIterator())
        {
            juce::PluginDescription description;
            if (description.loadFromXml (*child_element))
                result.add (new juce::PluginDescription (description));
        }

        return true;
    }

private:
    juce::File workerExecutable;
    std::function<bool()> shouldAbort;
    std::function<void (CrashedPluginRecord)> onCrashed;
};

/** The scan worker thread: walks the candidate list via
    PluginDirectoryScanner (which routes each file through the
    ChildProcessScannerImpl above and handles the incremental
    skip-unchanged-files logic), marshaling progress back to the message
    thread. */
class PluginScanner::ScanThread final : public juce::Thread
{
public:
    ScanThread (PluginScanner& ownerToUse,
                std::function<void (int, int, juce::String)> onProgressToUse,
                std::function<void (int, juce::StringArray)> onDoneToUse)
        : juce::Thread ("SG Plugin Scan"),
          owner (ownerToUse),
          onProgress (std::move (onProgressToUse)),
          onDone (std::move (onDoneToUse))
    {
    }

    ~ScanThread() override
    {
        // The child-wait loop polls threadShouldExit() every 200ms, so this
        // returns promptly even mid-candidate (the child gets killed).
        stopThread (10'000);
    }

    // Called from the scan thread (via ChildProcessScannerImpl).
    void recordCrash (CrashedPluginRecord record)
    {
        crashedThisScan.push_back (std::move (record));
    }

    void run() override
    {
        auto* format = [this]() -> juce::AudioPluginFormat*
        {
            for (auto* f : owner.formatManager.getFormats())
                if (f->getName() == "VST3")
                    return f;
            return nullptr;
        }();

        if (format != nullptr)
        {
            // Standard OS VST3 locations + any user-added folders from the
            // persisted cache.
            auto searchPath = format->getDefaultLocationsToSearch();
            for (const auto& folder : extraFolders)
                searchPath.addIfNotAlreadyThere (juce::File (folder));

            const auto candidates = format->searchPathsForPlugins (searchPath, true, false);
            const int total = candidates.size();

            juce::PluginDirectoryScanner scanner (owner.knownPlugins, *format, searchPath, true,
                                                  juce::File(), false);
            scanner.setFilesOrIdentifiersToScan (candidates);

            int done = 0;

            while (! threadShouldExit())
            {
                const auto currentName = juce::File (scanner.getNextPluginFileThatWillBeScanned())
                                             .getFileNameWithoutExtension();
                postProgress (done, total, currentName);

                juce::String nameOfPluginBeingScanned;
                if (! scanner.scanNextFile (true, nameOfPluginBeingScanned))
                    break;

                ++done;
            }

            postProgress (done, total, {});
        }

        owner.knownPlugins.scanFinished();
        postDone();
    }

    juce::StringArray extraFolders; // copied in before startThread(); scan-thread-only afterwards

private:
    void postProgress (int done, int total, juce::String currentName)
    {
        if (onProgress == nullptr)
            return;

        juce::MessageManager::callAsync ([cb = onProgress, done, total, name = std::move (currentName)]
        {
            cb (done, total, name);
        });
    }

    void postDone()
    {
        // Copies of everything the message thread needs -- this thread
        // object may be gone by the time the lambda runs.
        juce::WeakReference<PluginScanner> safeOwner (&owner);
        auto crashed = crashedThisScan;
        auto doneCallback = onDone;

        juce::MessageManager::callAsync ([safeOwner, crashed = std::move (crashed), doneCallback]
        {
            auto* scanner = safeOwner.get();
            if (scanner == nullptr)
                return;

            for (auto& record : crashed)
            {
                const auto alreadyRecorded = std::any_of (scanner->crashedRecords.begin(), scanner->crashedRecords.end(),
                    [&record] (const CrashedPluginRecord& existing) { return existing.path == record.path; });

                if (! alreadyRecorded)
                    scanner->crashedRecords.push_back (record);
            }

            scanner->saveCacheToDisk();

            if (doneCallback != nullptr)
            {
                juce::StringArray failedNames;
                for (const auto& record : crashed)
                    failedNames.add (record.name);

                doneCallback (scanner->knownPlugins.getNumTypes(), failedNames);
            }
        });
    }

    PluginScanner& owner;
    std::function<void (int, int, juce::String)> onProgress;
    std::function<void (int, juce::StringArray)> onDone;
    std::vector<CrashedPluginRecord> crashedThisScan; // scan-thread-only until run() posts a copy

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScanThread)
};

PluginScanner::PluginScanner()
{
    juce::addDefaultFormatsToManager (formatManager);
}

bool PluginScanner::isScanning() const noexcept
{
    return scanThread != nullptr && scanThread->isThreadRunning();
}

PluginScanner::~PluginScanner()
{
    scanThread.reset(); // joins (and kills any in-flight child) before members go away
    masterReference.clear();
}

void PluginScanner::loadCacheFromDisk()
{
    const auto file = getPluginCacheFile();
    if (! file.existsAsFile())
        return;

    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return;

    PluginCacheContents contents;
    if (! parsePluginCacheXml (*xml, contents))
        return;

    if (contents.knownPluginsXml != nullptr)
        knownPlugins.recreateFromXml (*contents.knownPluginsXml);

    extraScanFolders = contents.extraScanFolders;
    crashedRecords = contents.crashed;
}

void PluginScanner::saveCacheToDisk() const
{
    const auto listXml = knownPlugins.createXml();
    const auto cacheXml = createPluginCacheXml (listXml.get(), extraScanFolders, crashedRecords);

    const auto file = getPluginCacheFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (cacheXml->toString());
}

juce::Array<juce::PluginDescription> PluginScanner::getPluginsSortedByName() const
{
    auto types = knownPlugins.getTypes();

    std::sort (types.begin(), types.end(), [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
    {
        return a.name.compareIgnoreCase (b.name) < 0;
    });

    return types;
}

std::unique_ptr<juce::PluginDescription> PluginScanner::findByIdentifier (const juce::String& identifierString) const
{
    return knownPlugins.getTypeForIdentifierString (identifierString);
}

bool PluginScanner::startScan (const juce::File& workerExecutable,
                               std::function<void (int, int, juce::String)> onProgress,
                               std::function<void (int, juce::StringArray)> onDone)
{
    if (isScanning())
        return false;

    if (! workerExecutable.existsAsFile())
        return false;

    scanThread = std::make_unique<ScanThread> (*this, std::move (onProgress), std::move (onDone));
    scanThread->extraFolders = extraScanFolders;

    auto* thread = scanThread.get();
    knownPlugins.setCustomScanner (std::make_unique<ChildProcessScannerImpl> (
        workerExecutable,
        [thread] { return thread->threadShouldExit(); },
        [thread] (CrashedPluginRecord record) { thread->recordCrash (std::move (record)); }));

    scanThread->startThread();
    return true;
}

} // namespace sg
