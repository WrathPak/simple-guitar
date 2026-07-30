#pragma once

#include <vector>

#include <juce_core/juce_core.h>

/**
    Managed on-disk preset library: a flat folder alongside the existing
    Models/IRs libraries (see RigLibrary.h),

        Documents/Simple Guitar/Presets   (*.sgpreset)

    A .sgpreset file is human-readable JSON on purpose:

        { "schemaVersion": 1, "name": "<display name>", "state": "<APVTS XML text>" }

    `state` is the plain XML text of apvts.copyState().createXml() -- the
    same tree getStateInformation persists, just as readable text rather than
    getStateInformation's binary (copyXmlToBinary) encoding, so a preset file
    can be opened and read in a text editor. `schemaVersion` here is the
    on-disk preset file format version -- independent of (and much slower-
    moving than) the bridge protocol's SchemaVersion in schema/bridge.schema.json.

    All functions that touch the filesystem are message-thread only (like
    RigLibrary) -- never call from the audio thread. The JSON (de)serializing
    functions (presetFileJsonFor/parsePresetFileJson) are pure string
    functions with no filesystem access, kept separate so they're directly
    Catch2-testable; same spirit as PedalSlots.h being factored out for its
    own tests.
*/
namespace sg
{

struct PresetEntry
{
    juce::String name;
    juce::String path;
};

/** The on-disk .sgpreset JSON format version (see class comment). Bump if
    the file's field set ever changes shape. */
constexpr int presetFileSchemaVersion = 1;

struct PresetFileContents
{
    juce::String name;
    juce::String stateXmlText;
};

/** Documents/Simple Guitar/Presets. Does not touch the filesystem. */
juce::File getPresetsLibraryFolder();

/** Creates the Presets folder if it doesn't already exist. Safe to call
    repeatedly; message-thread only. */
void ensurePresetsFolderExists();

/** Flat, non-recursive scan of the Presets folder for *.sgpreset files,
    sorted by name (same convention as sg::scanLibrary). Message-thread
    only. */
std::vector<PresetEntry> scanPresets();

/** True if `file` resolves to a location inside the Presets folder --
    same validation sg::isInsideFolder gives the Models/IRs libraries. */
bool isInsidePresetsFolder (const juce::File& file);

/** Sanitizes an arbitrary user-typed preset name into a safe filename stem
    (no extension, no path separators or other filesystem-reserved
    characters). Returns an empty string if nothing sanitizable remains
    (e.g. an all-whitespace or all-illegal-character input) -- callers must
    treat that as an invalid name, not silently fall back to some default.
    Pure, no filesystem access -- directly testable. */
juce::String sanitizePresetFilename (const juce::String& rawName);

/** Serializes {schemaVersion, name, state} as pretty-printed JSON text --
    the exact .sgpreset on-disk format. Pure, no filesystem access. */
juce::String presetFileJsonFor (const juce::String& name, const juce::String& stateXmlText);

/** Parses .sgpreset JSON text back into its fields. Returns false (leaving
    outContents untouched) if the JSON is malformed, missing a required
    field, or its schemaVersion doesn't match presetFileSchemaVersion. Pure,
    no filesystem access. */
bool parsePresetFileJson (const juce::String& jsonText, PresetFileContents& outContents);

/** Reads and parses the .sgpreset file at `file`. Message-thread only
    (filesystem access). False on any failure (missing file, unreadable,
    malformed JSON). */
bool readPresetFile (const juce::File& file, PresetFileContents& outContents);

/** Writes `name`/`stateXmlText` as a .sgpreset JSON file at `file`,
    overwriting it if it already exists (overwrite-on-save-as is intentional,
    not an error). Message-thread only. False on a filesystem write
    failure. */
bool writePresetFile (const juce::File& file, const juce::String& name, const juce::String& stateXmlText);

} // namespace sg
