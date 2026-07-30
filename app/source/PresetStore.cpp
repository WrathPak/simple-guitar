#include "PresetStore.h"

#include <algorithm>

namespace sg
{

namespace
{
    constexpr const char* presetFileExtension = ".sgpreset";

    constexpr const char* schemaVersionKey = "schemaVersion";
    constexpr const char* nameKey = "name";
    constexpr const char* stateKey = "state";

    // Same Documents/Simple Guitar root RigLibrary.cpp uses for Models/IRs;
    // duplicated rather than shared because RigLibrary.cpp keeps its helper
    // in an anonymous namespace (private to that translation unit) -- this
    // is the one-line cost of PresetStore being its own file per RigLibrary's
    // own style rather than an extension of it.
    juce::File simpleGuitarDocumentsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("Simple Guitar");
    }
}

juce::File getPresetsLibraryFolder()
{
    return simpleGuitarDocumentsFolder().getChildFile ("Presets");
}

void ensurePresetsFolderExists()
{
    getPresetsLibraryFolder().createDirectory();
}

std::vector<PresetEntry> scanPresets()
{
    std::vector<PresetEntry> entries;
    const auto folder = getPresetsLibraryFolder();

    if (! folder.isDirectory())
        return entries;

    for (const auto& item : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFiles))
    {
        const auto file = item.getFile();

        if (file.getFileExtension().toLowerCase() != presetFileExtension)
            continue;

        entries.push_back ({ file.getFileNameWithoutExtension(), file.getFullPathName() });
    }

    std::sort (entries.begin(), entries.end(), [] (const PresetEntry& a, const PresetEntry& b)
    {
        return a.name.compareIgnoreCase (b.name) < 0;
    });

    return entries;
}

bool isInsidePresetsFolder (const juce::File& file)
{
    return file.isAChildOf (getPresetsLibraryFolder());
}

juce::String sanitizePresetFilename (const juce::String& rawName)
{
    auto result = rawName.trim();

    // Filesystem-reserved characters on Windows (also harmless to strip on
    // macOS/Linux, where only '/' is actually reserved) plus the path
    // separators -- replaced with '_' rather than dropped outright, so
    // e.g. "Lead/Rhythm" reads as "Lead_Rhythm" rather than "LeadRhythm".
    static const juce::String illegalChars = "\\/:*?\"<>|";

    for (auto ch : illegalChars)
        result = result.replaceCharacter (ch, '_');

    // Re-trim in case stripping left leading/trailing whitespace exposed
    // (e.g. a name that was only illegal characters and whitespace).
    return result.trim();
}

juce::String presetFileJsonFor (const juce::String& name, const juce::String& stateXmlText)
{
    juce::DynamicObject::Ptr obj (new juce::DynamicObject());
    obj->setProperty (schemaVersionKey, presetFileSchemaVersion);
    obj->setProperty (nameKey, name);
    obj->setProperty (stateKey, stateXmlText);
    return juce::JSON::toString (juce::var (obj.get()));
}

bool parsePresetFileJson (const juce::String& jsonText, PresetFileContents& outContents)
{
    const auto parsed = juce::JSON::parse (jsonText);
    auto* obj = parsed.getDynamicObject();

    if (obj == nullptr)
        return false;

    const auto schemaVersionVar = obj->getProperty (schemaVersionKey);
    if (! (schemaVersionVar.isInt() || schemaVersionVar.isDouble() || schemaVersionVar.isInt64()))
        return false;
    if ((int) schemaVersionVar != presetFileSchemaVersion)
        return false;

    const auto nameVar = obj->getProperty (nameKey);
    const auto stateVar = obj->getProperty (stateKey);
    if (! nameVar.isString() || ! stateVar.isString())
        return false;

    outContents.name = nameVar.toString();
    outContents.stateXmlText = stateVar.toString();
    return true;
}

bool readPresetFile (const juce::File& file, PresetFileContents& outContents)
{
    if (! file.existsAsFile())
        return false;

    return parsePresetFileJson (file.loadFileAsString(), outContents);
}

bool writePresetFile (const juce::File& file, const juce::String& name, const juce::String& stateXmlText)
{
    return file.replaceWithText (presetFileJsonFor (name, stateXmlText));
}

} // namespace sg
