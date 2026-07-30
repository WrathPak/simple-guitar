#pragma once

#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

/**
    Pure helpers for the out-of-process plugin scanner (see PluginHosting.h
    for the runtime pieces): the child-process stdout protocol and the
    on-disk plugin cache XML. No filesystem access, no juce_audio_processors
    dependency -- everything here operates on plain strings/XmlElement so
    it's directly Catch2-testable (same spirit as PresetStore's pure JSON
    half and StateMigration).

    Child-process protocol: the Standalone executable, launched as
    `"<exe>" --scan-plugin "<path>"`, scans exactly one candidate plugin
    file in-process and prints wrapScanResultXml(<xml of the descriptions
    found>) to stdout, then exits. The parent extracts the payload with
    extractScanResultXml() -- sentinel markers bracket the XML so anything
    the plugin itself spews to stdout during its own static init can't
    corrupt the parse. No sentinel-bracketed payload in the output (child
    crashed, hung, or printed garbage) reads as a scan failure and the
    parent blacklists the file.

    Cache file: Documents/Simple Guitar/plugins.xml, root tag SGPLUGINS,
    holding
      - the KnownPluginList's own "KNOWNPLUGINS" element verbatim (which
        already carries both the scanned types and the BLACKLISTED file
        entries -- see juce::KnownPluginList::createXml()),
      - zero or more FOLDER elements (path attribute): user-added scan
        folders on top of the standard OS VST3 locations. Nothing writes
        these yet (the UI wave adds the management surface) but the format
        carries them so adding that surface won't need a cache migration.
      - zero or more CRASHED elements (name + path attributes): candidates
        that failed a scan, recorded by display name so the UI can report
        *what* crashed (the blacklist inside KNOWNPLUGINS only knows the
        file path).
*/
namespace sg
{

/** One crashed-candidate record in the plugin cache (see file comment). */
struct CrashedPluginRecord
{
    juce::String name;
    juce::String path;
};

/** Parsed contents of the plugins.xml cache file. */
struct PluginCacheContents
{
    /** The KnownPluginList's own XML ("KNOWNPLUGINS"), or null if the cache
        didn't carry one. Kept opaque here -- callers hand it straight to
        juce::KnownPluginList::recreateFromXml(). */
    std::unique_ptr<juce::XmlElement> knownPluginsXml;

    juce::StringArray extraScanFolders;
    std::vector<CrashedPluginRecord> crashed;
};

/** Root tag of the plugins.xml cache file. */
constexpr const char* pluginCacheRootTag = "SGPLUGINS";

/** Sentinel lines bracketing the scan-result XML in the child process's
    stdout (see file comment). */
constexpr const char* scanOutputBeginMarker = "SG_SCAN_RESULT_BEGIN";
constexpr const char* scanOutputEndMarker = "SG_SCAN_RESULT_END";

/** Wraps the XML text of a scan result (any single-root XML document text,
    conventionally a "SCANRESULT" element holding one child per
    PluginDescription found) in the sentinel markers for printing to the
    child process's stdout. Pure. */
juce::String wrapScanResultXml (const juce::String& resultXmlText);

/** Extracts and parses the sentinel-bracketed XML payload from a child
    process's captured stdout. Returns null if either marker is missing or
    the payload between them isn't parseable XML -- callers treat null as
    "the child crashed or produced garbage" and blacklist the candidate.
    Pure. */
std::unique_ptr<juce::XmlElement> extractScanResultXml (const juce::String& childOutput);

/** Builds the plugins.xml root element from its parts (see file comment for
    the format). `knownPluginsXml` may be null (serializes as a cache with no
    scanned list yet). Pure. */
std::unique_ptr<juce::XmlElement> createPluginCacheXml (const juce::XmlElement* knownPluginsXml,
                                                        const juce::StringArray& extraScanFolders,
                                                        const std::vector<CrashedPluginRecord>& crashed);

/** Parses a plugins.xml root element back into its parts. Returns false
    (leaving outContents untouched) if `root` isn't an SGPLUGINS element;
    unknown child elements are ignored (forward compatibility). Pure. */
bool parsePluginCacheXml (const juce::XmlElement& root, PluginCacheContents& outContents);

} // namespace sg
