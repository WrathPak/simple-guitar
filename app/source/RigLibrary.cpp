#include "RigLibrary.h"

#include <algorithm>

namespace sg
{

namespace
{
    juce::File simpleGuitarDocumentsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("Simple Guitar");
    }

    std::vector<LibraryEntry> scanFolderForExtensions (const juce::File& folder, const juce::StringArray& extensions)
    {
        std::vector<LibraryEntry> entries;

        if (! folder.isDirectory())
            return entries;

        for (const auto& item : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFiles))
        {
            const auto file = item.getFile();
            const auto extension = file.getFileExtension().toLowerCase();

            if (! extensions.contains (extension))
                continue;

            entries.push_back ({ file.getFileNameWithoutExtension(), file.getFullPathName() });
        }

        std::sort (entries.begin(), entries.end(), [] (const LibraryEntry& a, const LibraryEntry& b)
        {
            return a.name.compareIgnoreCase (b.name) < 0;
        });

        return entries;
    }
}

juce::File getModelsLibraryFolder()
{
    return simpleGuitarDocumentsFolder().getChildFile ("Models");
}

juce::File getIrsLibraryFolder()
{
    return simpleGuitarDocumentsFolder().getChildFile ("IRs");
}

void ensureLibraryFoldersExist()
{
    getModelsLibraryFolder().createDirectory();
    getIrsLibraryFolder().createDirectory();
}

RigLibrary scanLibrary()
{
    RigLibrary library;
    library.models = scanFolderForExtensions (getModelsLibraryFolder(), { ".nam" });
    library.irs = scanFolderForExtensions (getIrsLibraryFolder(), { ".wav", ".aiff" });
    return library;
}

bool isInsideFolder (const juce::File& file, const juce::File& folder)
{
    return file.isAChildOf (folder);
}

} // namespace sg
