// Validates every bundled factory preset (content/presets/*.sgpreset -- see
// content/ATTRIBUTION.md) round-trips through PresetStore's own parser
// (sg::parsePresetFileJson) and that the embedded APVTS state XML has the
// shape PluginProcessor actually writes/reads: root tag "PARAMETERS", one
// <PARAM id=".." value=".."/> child per known param id, and
// namModelPath/irPath/chainOrder attributes on the root.
//
// namModelPath/irPath specifically must be BARE FILENAMES as shipped in the
// repo -- no path separators (either '/' or '\\'), no drive letters. Baking
// an absolute authoring-machine path into a file committed to a public repo
// leaks the machine's username/home directory and, on top of that, breaks
// cross-platform (a backslash-separated Windows path doesn't parse as a
// path on macOS/Linux, so a naive basename-by-juce::File::getFileName()
// check silently passes on Windows and fails on macOS -- ContentInstaller.h/
// ContentInstallerCore.cpp's separator-agnostic lastPathComponent() is what
// actually resolves these correctly at install time; this test checks the
// bundled *source* files never regress back to an absolute path in the
// first place, independent of that install-time logic).
//
// This is a content-verification test, not a PresetStore API test -- it
// exists so a hand-authored preset with a typo'd param id, a missing
// attribute, a leaked absolute path, or a filename that doesn't match any
// bundled file fails the build instead of silently shipping a broken (or
// privacy-leaking) factory preset.
//
// SG_CONTENT_PRESETS_DIR/SG_CONTENT_MODELS_DIR/SG_CONTENT_IRS_DIR are defined
// by tests/CMakeLists.txt to content/{presets,models,irs}'s absolute paths.

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include "ContentInstaller.h"
#include "PresetStore.h"

#include <array>
#include <vector>

#if !defined (SG_CONTENT_PRESETS_DIR) || !defined (SG_CONTENT_MODELS_DIR) || !defined (SG_CONTENT_IRS_DIR)
  #error "SG_CONTENT_PRESETS_DIR/SG_CONTENT_MODELS_DIR/SG_CONTENT_IRS_DIR must be defined by the build " \
         "-- see tests/CMakeLists.txt."
#endif

namespace
{
    // Mirrors SimpleGuitarAudioProcessor::allParamIds (PluginProcessor.h) --
    // duplicated rather than included, since pulling in PluginProcessor.h
    // here would drag juce_audio_processors/juce_dsp/juce_gui_extra into a
    // test binary that otherwise only needs juce_core (see PresetStore.h's
    // own "message-thread only, pure JSON" design).
    constexpr std::array<const char*, 24> kAllParamIds { {
        "outputGain", "gateOn", "gateThresholdDb", "ampInputDb", "ampBassDb",
        "ampMidDb", "ampTrebleDb", "ampPresenceDb", "namNormalize", "cabOn",
        "cabLowCutHz", "cabHighCutHz", "screamerOn", "screamerDrive",
        "screamerTone", "screamerLevel", "echoesOn", "echoesTime",
        "echoesFeedback", "echoesMix", "chamberOn", "chamberDecay",
        "chamberTone", "chamberMix"
    } };

    std::vector<juce::File> listBundledPresetFiles()
    {
        const juce::File presetsDir (SG_CONTENT_PRESETS_DIR);
        std::vector<juce::File> files;

        for (const auto& item : juce::RangedDirectoryIterator (presetsDir, false, "*.sgpreset", juce::File::findFiles))
            files.push_back (item.getFile());

        return files;
    }

    // True iff `value` is a bare filename: no '/' or '\\' anywhere, and no
    // ':' (rules out a Windows drive letter, e.g. "C:..."). This is a
    // stricter, platform-independent check than "does lastPathComponent()
    // resolve to an existing file" -- it fails loudly on an absolute path
    // even on a platform where that path happens to still resolve.
    bool isBareFilename (const juce::String& value)
    {
        return value.isNotEmpty()
            && ! value.containsChar ('/')
            && ! value.containsChar ('\\')
            && ! value.containsChar (':');
    }

    bool fileExistsByBaseName (const juce::File& dir, const juce::String& value)
    {
        const auto baseName = sg::lastPathComponent (value);
        return baseName.isNotEmpty() && dir.getChildFile (baseName).existsAsFile();
    }
}

TEST_CASE ("every bundled factory preset round-trips through PresetStore's parser", "[content][PresetStore]")
{
    const auto files = listBundledPresetFiles();

    REQUIRE (! files.empty());

    const juce::File modelsDir (SG_CONTENT_MODELS_DIR);
    const juce::File irsDir (SG_CONTENT_IRS_DIR);

    for (const auto& file : files)
    {
        INFO ("preset file: " << file.getFileName());

        sg::PresetFileContents contents;
        REQUIRE (sg::readPresetFile (file, contents));
        CHECK (contents.name.isNotEmpty());
        CHECK (contents.stateXmlText.isNotEmpty());

        // Re-serializing and re-parsing must reproduce the same fields --
        // the actual "round-trip" this test is named for.
        const auto reserialized = sg::presetFileJsonFor (contents.name, contents.stateXmlText);
        sg::PresetFileContents roundTripped;
        REQUIRE (sg::parsePresetFileJson (reserialized, roundTripped));
        CHECK (roundTripped.name == contents.name);
        CHECK (roundTripped.stateXmlText == contents.stateXmlText);

        const std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (contents.stateXmlText));
        REQUIRE (xml != nullptr);
        CHECK (xml->hasTagName ("PARAMETERS"));

        for (auto* paramId : kAllParamIds)
        {
            INFO ("missing/duplicate param id: " << paramId);

            int matchCount = 0;
            for (auto* child : xml->getChildIterator())
                if (child->hasTagName ("PARAM") && child->getStringAttribute ("id") == juce::String (paramId))
                    ++matchCount;

            CHECK (matchCount == 1);
        }

        REQUIRE (xml->hasAttribute ("namModelPath"));
        REQUIRE (xml->hasAttribute ("irPath"));
        REQUIRE (xml->hasAttribute ("chainOrder"));

        const auto namModelPath = xml->getStringAttribute ("namModelPath");
        const auto irPath = xml->getStringAttribute ("irPath");

        // The shipped attribute values themselves must already be bare
        // filenames -- not merely "resolvable to one after stripping a
        // path prefix". A regression back to an absolute authoring-machine
        // path must fail here on every platform (see the file-level comment).
        CHECK (isBareFilename (namModelPath));
        CHECK (isBareFilename (irPath));

        CHECK (fileExistsByBaseName (modelsDir, namModelPath));
        CHECK (fileExistsByBaseName (irsDir, irPath));

        // chainOrder must be a comma-joined permutation of the three known
        // pedal ids (see ChainOrder.h / PluginProcessor's pedalIdForSlot).
        const auto chainOrderIds = juce::StringArray::fromTokens (xml->getStringAttribute ("chainOrder"), ",", "");
        CHECK (chainOrderIds.size() == 3);
        CHECK (chainOrderIds.contains ("screamer"));
        CHECK (chainOrderIds.contains ("echoes"));
        CHECK (chainOrderIds.contains ("chamber"));
    }
}
