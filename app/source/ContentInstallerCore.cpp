// Pure(r) helpers factored out of ContentInstaller.cpp specifically so they
// can be linked into the test binary (tests/app/ContentInstallerTest.cpp)
// without pulling in BinaryData.h / juce::ZipFile -- this file only touches
// juce_core (File, XmlDocument, XmlElement). See ContentInstaller.h for the
// full design rationale.

#include "ContentInstaller.h"

namespace sg
{

bool copyIfAbsent (const juce::File& destFile, const void* data, size_t numBytes)
{
    if (destFile.existsAsFile())
        return false; // already there (factory original or a user file with the same name) -- never overwrite.

    if (! destFile.getParentDirectory().createDirectory())
        return false;

    return destFile.replaceWithData (data, numBytes);
}

juce::String lastPathComponent (const juce::String& pathOrFilename)
{
    // Separator-agnostic on purpose -- see the header comment. A bare
    // filename (the normal case for bundled content) has neither character
    // and passes through unchanged.
    const auto lastSeparator = juce::jmax (pathOrFilename.lastIndexOfChar ('/'),
                                            pathOrFilename.lastIndexOfChar ('\\'));

    return lastSeparator >= 0 ? pathOrFilename.substring (lastSeparator + 1) : pathOrFilename;
}

juce::String rewritePresetPathsForThisMachine (const juce::String& stateXmlText,
                                                const juce::File& modelsFolder,
                                                const juce::File& irsFolder)
{
    const std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (stateXmlText));

    if (xml == nullptr)
        return stateXmlText; // leave malformed input untouched -- caller decides what to do with it.

    constexpr const char* namModelPathAttr = "namModelPath";
    constexpr const char* irPathAttr = "irPath";

    if (xml->hasAttribute (namModelPathAttr))
    {
        const auto baseName = lastPathComponent (xml->getStringAttribute (namModelPathAttr));
        if (baseName.isNotEmpty())
            xml->setAttribute (namModelPathAttr, modelsFolder.getChildFile (baseName).getFullPathName());
    }

    if (xml->hasAttribute (irPathAttr))
    {
        const auto baseName = lastPathComponent (xml->getStringAttribute (irPathAttr));
        if (baseName.isNotEmpty())
            xml->setAttribute (irPathAttr, irsFolder.getChildFile (baseName).getFullPathName());
    }

    return xml->toString();
}

} // namespace sg
