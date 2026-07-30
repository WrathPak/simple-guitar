#include "ContentInstaller.h"

#include <ContentBinaryData.h>

#include "PresetStore.h"
#include "RigLibrary.h"

// copyIfAbsent()/rewritePresetPathsForThisMachine() (the pure helpers this
// file drives) live in ContentInstallerCore.cpp -- split out so they can be
// linked into the test binary without pulling in BinaryData.h/juce::ZipFile
// (see that file's own header comment, and ContentInstaller.h).

namespace sg
{

namespace
{
    // Extension-based routing for the three content kinds inside the zip --
    // mirrors RigLibrary::scanFolderForExtensions's own extension checks.
    bool hasExtension (const juce::String& filename, const juce::StringArray& extensions)
    {
        return extensions.contains (juce::File (filename).getFileExtension().toLowerCase());
    }

    void installModelOrIr (juce::ZipFile& zip, int entryIndex, const juce::File& destFolder)
    {
        auto* entry = zip.getEntry (entryIndex);
        if (entry == nullptr)
            return;

        const auto baseName = lastPathComponent (entry->filename);
        const std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (entryIndex));

        if (stream == nullptr)
            return;

        juce::MemoryBlock data;
        stream->readIntoMemoryBlock (data);

        copyIfAbsent (destFolder.getChildFile (baseName), data.getData(), data.getSize());
    }

    void installPreset (juce::ZipFile& zip, int entryIndex)
    {
        auto* entry = zip.getEntry (entryIndex);
        if (entry == nullptr)
            return;

        const auto baseName = lastPathComponent (entry->filename);
        const auto destFile = sg::getPresetsLibraryFolder().getChildFile (baseName);

        if (destFile.existsAsFile())
            return; // copy-if-absent -- same rule as models/IRs.

        const std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (entryIndex));
        if (stream == nullptr)
            return;

        const auto jsonText = stream->readEntireStreamAsString();

        sg::PresetFileContents contents;
        if (! sg::parsePresetFileJson (jsonText, contents))
            return; // malformed bundled preset -- nothing sensible to install.

        const auto rewrittenXml = rewritePresetPathsForThisMachine (
            contents.stateXmlText, sg::getModelsLibraryFolder(), sg::getIrsLibraryFolder());

        sg::writePresetFile (destFile, contents.name, rewrittenXml);
    }
}

void installBundledContent()
{
    sg::ensureLibraryFoldersExist();
    sg::ensurePresetsFolderExists();

    static juce::MemoryInputStream zipStream (ContentBinaryData::sgcontent_zip, (size_t) ContentBinaryData::sgcontent_zipSize, false);
    juce::ZipFile zip (zipStream);

    const auto modelsFolder = sg::getModelsLibraryFolder();
    const auto irsFolder = sg::getIrsLibraryFolder();

    static const juce::StringArray namExtensions { ".nam" };
    static const juce::StringArray irExtensions { ".wav", ".aiff" };

    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        auto* entry = zip.getEntry (i);
        if (entry == nullptr || entry->filename.endsWithChar ('/'))
            continue; // skip directory entries.

        const auto path = entry->filename;

        if (path.startsWith ("models/") && hasExtension (path, namExtensions))
            installModelOrIr (zip, i, modelsFolder);
        else if (path.startsWith ("irs/") && hasExtension (path, irExtensions))
            installModelOrIr (zip, i, irsFolder);
        else if (path.startsWith ("presets/") && path.endsWithIgnoreCase (".sgpreset"))
            installPreset (zip, i);
        // Anything else (ATTRIBUTION.md, etc.) is repo/legal content, not something to install.
    }
}

} // namespace sg
