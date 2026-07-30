#include "PluginScanCore.h"

namespace sg
{

namespace
{
    constexpr const char* folderTag = "FOLDER";
    constexpr const char* crashedTag = "CRASHED";
    constexpr const char* knownPluginsTag = "KNOWNPLUGINS"; // juce::KnownPluginList::createXml()'s own root tag
    constexpr const char* pathAttr = "path";
    constexpr const char* nameAttr = "name";
}

juce::String wrapScanResultXml (const juce::String& resultXmlText)
{
    // Leading newline isolates the begin marker onto its own line even if
    // the plugin's static init already printed a partial line to stdout.
    return "\n" + juce::String (scanOutputBeginMarker) + "\n"
         + resultXmlText.trim() + "\n"
         + juce::String (scanOutputEndMarker) + "\n";
}

std::unique_ptr<juce::XmlElement> extractScanResultXml (const juce::String& childOutput)
{
    const int begin = childOutput.indexOf (scanOutputBeginMarker);
    if (begin < 0)
        return nullptr;

    const int payloadStart = begin + (int) juce::String (scanOutputBeginMarker).length();
    const int end = childOutput.indexOf (payloadStart, scanOutputEndMarker);
    if (end < 0)
        return nullptr;

    const auto payload = childOutput.substring (payloadStart, end).trim();
    if (payload.isEmpty())
        return nullptr;

    return juce::XmlDocument::parse (payload);
}

std::unique_ptr<juce::XmlElement> createPluginCacheXml (const juce::XmlElement* knownPluginsXml,
                                                        const juce::StringArray& extraScanFolders,
                                                        const std::vector<CrashedPluginRecord>& crashed)
{
    auto root = std::make_unique<juce::XmlElement> (pluginCacheRootTag);

    if (knownPluginsXml != nullptr)
        root->addChildElement (new juce::XmlElement (*knownPluginsXml));

    for (const auto& folder : extraScanFolders)
    {
        auto* folderElement = root->createNewChildElement (folderTag);
        folderElement->setAttribute (pathAttr, folder);
    }

    for (const auto& record : crashed)
    {
        auto* crashedElement = root->createNewChildElement (crashedTag);
        crashedElement->setAttribute (nameAttr, record.name);
        crashedElement->setAttribute (pathAttr, record.path);
    }

    return root;
}

bool parsePluginCacheXml (const juce::XmlElement& root, PluginCacheContents& outContents)
{
    if (! root.hasTagName (pluginCacheRootTag))
        return false;

    PluginCacheContents parsed;

    for (const auto* child : root.getChildIterator())
    {
        if (child->hasTagName (knownPluginsTag))
        {
            // Last one wins if a hand-edited file carries duplicates.
            parsed.knownPluginsXml = std::make_unique<juce::XmlElement> (*child);
        }
        else if (child->hasTagName (folderTag))
        {
            const auto path = child->getStringAttribute (pathAttr);
            if (path.isNotEmpty())
                parsed.extraScanFolders.add (path);
        }
        else if (child->hasTagName (crashedTag))
        {
            CrashedPluginRecord record;
            record.name = child->getStringAttribute (nameAttr);
            record.path = child->getStringAttribute (pathAttr);
            if (record.name.isNotEmpty() || record.path.isNotEmpty())
                parsed.crashed.push_back (std::move (record));
        }
        // Unknown tags: ignored (forward compatibility).
    }

    outContents = std::move (parsed);
    return true;
}

} // namespace sg
