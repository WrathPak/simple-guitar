#pragma once

#include <juce_core/juce_core.h>

/**
    First-run install of the bundled factory content (NAM models, cab IRs,
    presets) into the managed Documents/Simple Guitar folders (see
    RigLibrary.h / PresetStore.h for those folders' layout).

    The content itself -- content/models/*.nam, content/irs/*.wav,
    content/presets/*.sgpreset, plus content/ATTRIBUTION.md -- lives in the
    repo under content/ and is zipped and embedded as BinaryData at build
    time (see app/CMakeLists.txt's sg_content_bundle target, which mirrors
    the ui/dist embedding pattern immediately above it). installBundledContent()
    reads that embedded zip via juce::ZipFile and extracts it.

    Copy-if-absent, per file: a file already present at the destination
    (whether it's the factory original from a previous run, or the user's own
    file that happens to share the name) is left untouched. This is what
    makes the install idempotent and non-destructive -- deleting one factory
    file and relaunching restores only that file, and a user's own edited
    version of a same-named file is never clobbered.

    Presets are the one wrinkle: a loaded/restored .sgpreset's namModelPath/
    irPath are normally absolute filesystem paths (see PresetStore.h and
    PluginProcessor's setStateInformation/restoreLoadedPathsFromState), but
    the BUNDLED presets under content/presets/ deliberately ship with bare
    filenames only ("brit crunch.nam", not an absolute path) -- baking in any
    real machine's absolute path (drive letter, username, home directory)
    into a file committed to a public repo is both a privacy leak and
    non-portable across platforms (backslash-separated Windows paths don't
    parse as paths on macOS/Linux). So presets are not a byte-for-byte copy
    like models/IRs -- installBundledContent() parses each bundled preset's
    JSON+XML (via PresetStore's own parsePresetFileJson/presetFileJsonFor),
    rewrites namModelPath/irPath to this machine's real Models/IRs library
    folders (keeping just the last path component from whatever was in the
    bundled file -- see lastPathComponent() below, which handles a bare
    filename, or, defensively, a legacy/foreign absolute path using either
    separator -- recomputed against
    getModelsLibraryFolder()/getIrsLibraryFolder()), and writes the corrected
    file out -- still copy-if-absent by destination filename.

    Message-thread only (filesystem + BinaryData access) -- never call from
    the audio thread. Cheap and safe to call unconditionally on every
    startup, same as sg::ensureLibraryFoldersExist()/ensurePresetsFolderExists().

    Split across two .cpp files: ContentInstaller.cpp has installBundledContent()
    itself (the BinaryData/juce::ZipFile-driven part); ContentInstallerCore.cpp
    has the two pure(r) helpers below, kept free of the BinaryData/ZipFile
    dependency specifically so they can be linked into the test binary (see
    tests/app/ContentInstallerTest.cpp) without pulling those in -- same
    spirit as PedalSlots.h/PresetStore.h being factored out for their own
    Catch2 tests.
*/
namespace sg
{

/** Extracts the embedded factory content zip and installs every model, IR,
    and preset it contains into the managed Documents/Simple Guitar folders,
    copy-if-absent per file (see class-level comment above). Creates the
    Models/IRs/Presets folders first if needed. Message-thread only. */
void installBundledContent();

//==============================================================================
// Pure(r) helpers factored out of installBundledContent() so the install
// logic's actual decisions are directly Catch2-testable against a temp
// directory, without needing the real embedded zip or a real Documents
// folder -- see tests/app/ContentInstallerTest.cpp.

/** Writes `data` (`numBytes` long) to `destFile` only if it doesn't already
    exist -- an existing file (whether a factory original from a previous
    run, or a user's own file with the same name) is left untouched. Creates
    `destFile`'s parent directory if needed. Returns true if the file was
    written, false if it already existed or the write failed. Filesystem
    access -- message-thread only. */
bool copyIfAbsent (const juce::File& destFile, const void* data, size_t numBytes);

/** Returns the last path component of `pathOrFilename`, splitting on
    whichever of '/' or '\\' appears last (both, not just whatever the
    current platform's native separator is) -- unlike juce::File::getFileName(),
    which only treats '\\' as a separator on Windows, so a Windows-style
    absolute path fed through it on macOS/Linux would otherwise come back
    as one giant "filename" full of literal backslashes. Bundled content's
    namModelPath/irPath are bare filenames with no separators at all (see
    class-level comment), so this is normally a no-op passthrough; it exists
    to still do the right thing with any legacy or foreign-platform absolute
    path. Returns `pathOrFilename` unchanged if it contains no separator.
    Pure string manipulation, no filesystem access. */
juce::String lastPathComponent (const juce::String& pathOrFilename);

/** Rewrites a preset's namModelPath/irPath attributes (see PresetStore.h's
    on-disk format) so they point at this machine's real Models/IRs library
    folders, keeping only the last path component (see lastPathComponent())
    of whatever was in `stateXmlText` -- ordinarily a bare filename (see
    class-level comment on bundled presets), but a full path is handled the
    same way. Pure string/XML manipulation, no filesystem access -- returns
    `stateXmlText` unchanged if it doesn't parse as XML. Missing
    namModelPath/irPath attributes (or an empty filename) are left alone
    rather than guessed at. */
juce::String rewritePresetPathsForThisMachine (const juce::String& stateXmlText,
                                                const juce::File& modelsFolder,
                                                const juce::File& irsFolder);

} // namespace sg
