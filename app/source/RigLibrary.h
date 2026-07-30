#pragma once

#include <vector>

#include <juce_core/juce_core.h>

/**
    Managed on-disk library for NAM models and cab IRs: two flat folders
    under the user's Documents directory,

        Documents/Simple Guitar/Models   (*.nam)
        Documents/Simple Guitar/IRs      (*.wav, *.aiff)

    Message-thread only (touches the filesystem) -- never call from the
    audio thread. Scanning is a flat, non-recursive listing: this is a
    managed library, not a general file browser.
*/
namespace sg
{

struct LibraryEntry
{
    juce::String name;
    juce::String path;
};

struct RigLibrary
{
    std::vector<LibraryEntry> models;
    std::vector<LibraryEntry> irs;
};

/** Documents/Simple Guitar/Models. Does not touch the filesystem. */
juce::File getModelsLibraryFolder();

/** Documents/Simple Guitar/IRs. Does not touch the filesystem. */
juce::File getIrsLibraryFolder();

/** Creates both library folders if they don't already exist. Safe to call
    repeatedly; message-thread only. */
void ensureLibraryFoldersExist();

/** Scans both library folders (flat, non-recursive) for .nam models and
    .wav/.aiff IRs, sorted by name. Message-thread only. */
RigLibrary scanLibrary();

/** True if `file` resolves to a location inside `folder` (recursively).
    Used to validate that a UI-supplied load path is confined to the
    managed library rather than an arbitrary filesystem path. */
bool isInsideFolder (const juce::File& file, const juce::File& folder);

} // namespace sg
