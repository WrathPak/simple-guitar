// Catch2 tests for the pure(r) helpers behind first-run content install
// (app/source/ContentInstaller.h, implemented in ContentInstallerCore.cpp):
// copyIfAbsent() and rewritePresetPathsForThisMachine(). These are the two
// decisions installBundledContent() actually makes; testing them directly
// against a temp directory avoids needing the real embedded content zip or
// a real Documents folder -- same spirit as ChainOrder.h/PresetStore.h being
// factored out for their own filesystem-free/JUCE-free Catch2 tests.

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include "ContentInstaller.h"

#include <cstring>

namespace
{
    // RAII temp directory, unique per test case so parallel/rerun test runs
    // never collide.
    struct TempDir
    {
        juce::File dir;

        TempDir()
            : dir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getNonexistentChildFile ("sg_content_installer_test", "", false))
        {
            dir.createDirectory();
        }

        ~TempDir()
        {
            dir.deleteRecursively();
        }
    };

    const char* kPayload = "hello factory content";
}

TEST_CASE ("copyIfAbsent writes a new file that doesn't exist yet", "[content][ContentInstaller]")
{
    TempDir temp;
    const auto dest = temp.dir.getChildFile ("models").getChildFile ("brit crunch.nam");

    REQUIRE_FALSE (dest.existsAsFile());

    const bool wrote = sg::copyIfAbsent (dest, kPayload, strlen (kPayload));

    CHECK (wrote);
    REQUIRE (dest.existsAsFile());
    CHECK (dest.loadFileAsString() == juce::String (kPayload));
}

TEST_CASE ("copyIfAbsent never overwrites an existing file", "[content][ContentInstaller]")
{
    TempDir temp;
    const auto dest = temp.dir.getChildFile ("brit crunch.nam");

    const char* usersOwnContent = "this is the user's own file, not the factory one";
    dest.replaceWithText (usersOwnContent);

    const bool wrote = sg::copyIfAbsent (dest, kPayload, strlen (kPayload));

    CHECK_FALSE (wrote);
    CHECK (dest.loadFileAsString() == juce::String (usersOwnContent));
}

TEST_CASE ("copyIfAbsent creates missing parent directories", "[content][ContentInstaller]")
{
    TempDir temp;
    const auto dest = temp.dir.getChildFile ("nested").getChildFile ("deeper").getChildFile ("ir.wav");

    REQUIRE_FALSE (dest.getParentDirectory().isDirectory());

    const bool wrote = sg::copyIfAbsent (dest, kPayload, strlen (kPayload));

    CHECK (wrote);
    CHECK (dest.existsAsFile());
}

TEST_CASE ("copyIfAbsent reinstalls a file that was deleted after a previous install", "[content][ContentInstaller]")
{
    TempDir temp;
    const auto dest = temp.dir.getChildFile ("brit crunch.nam");

    REQUIRE (sg::copyIfAbsent (dest, kPayload, strlen (kPayload)));
    REQUIRE (dest.existsAsFile());

    dest.deleteFile();
    REQUIRE_FALSE (dest.existsAsFile());

    // Missing again (deleted, not present) -- copy-if-absent means this
    // resurrects it, same as a genuine first run.
    const bool wroteAgain = sg::copyIfAbsent (dest, kPayload, strlen (kPayload));
    CHECK (wroteAgain);
    CHECK (dest.existsAsFile());
}

TEST_CASE ("lastPathComponent passes a bare filename through unchanged", "[content][ContentInstaller]")
{
    CHECK (sg::lastPathComponent ("brit crunch.nam") == "brit crunch.nam");
}

TEST_CASE ("lastPathComponent splits on a forward slash", "[content][ContentInstaller]")
{
    CHECK (sg::lastPathComponent ("models/brit crunch.nam") == "brit crunch.nam");
    CHECK (sg::lastPathComponent ("/Users/someone/Documents/Simple Guitar/Models/brit crunch.nam") == "brit crunch.nam");
}

TEST_CASE ("lastPathComponent splits on a backslash even when running on a platform where that's not the native separator", "[content][ContentInstaller]")
{
    // This is exactly the bug the coordinator caught in CI: on macOS/Linux,
    // juce::File::getFileName() does NOT treat '\\' as a separator, so a
    // Windows-authored absolute path fed through it comes back as one giant
    // "filename" -- lastPathComponent() must split on '\\' unconditionally,
    // not just on the current platform's native separator.
    CHECK (sg::lastPathComponent ("C:\\Users\\someone\\Documents\\Simple Guitar\\Models\\brit crunch.nam") == "brit crunch.nam");
}

TEST_CASE ("lastPathComponent handles a path with both separators, splitting on whichever is last", "[content][ContentInstaller]")
{
    CHECK (sg::lastPathComponent ("C:\\Users\\someone/Documents\\brit crunch.nam") == "brit crunch.nam");
}

TEST_CASE ("rewritePresetPathsForThisMachine rewrites namModelPath/irPath to the given folders, keeping the filename", "[content][ContentInstaller]")
{
    const juce::String stateXml =
        "<PARAMETERS namModelPath=\"C:\\Users\\someoneelse\\Documents\\Simple Guitar\\Models\\brit crunch.nam\" "
        "irPath=\"C:\\Users\\someoneelse\\Documents\\Simple Guitar\\IRs\\warm straight.wav\" "
        "chainOrder=\"screamer,echoes,chamber\">"
        "<PARAM id=\"outputGain\" value=\"0.0\"/>"
        "</PARAMETERS>";

    const juce::File modelsFolder ("D:\\ThisMachine\\Documents\\Simple Guitar\\Models");
    const juce::File irsFolder ("D:\\ThisMachine\\Documents\\Simple Guitar\\IRs");

    const auto rewritten = sg::rewritePresetPathsForThisMachine (stateXml, modelsFolder, irsFolder);

    const std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (rewritten));
    REQUIRE (xml != nullptr);

    CHECK (xml->getStringAttribute ("namModelPath") == modelsFolder.getChildFile ("brit crunch.nam").getFullPathName());
    CHECK (xml->getStringAttribute ("irPath") == irsFolder.getChildFile ("warm straight.wav").getFullPathName());

    // Untouched attributes/children survive the rewrite.
    CHECK (xml->getStringAttribute ("chainOrder") == "screamer,echoes,chamber");
    CHECK (xml->getNumChildElements() == 1);
}

TEST_CASE ("rewritePresetPathsForThisMachine leaves malformed XML untouched", "[content][ContentInstaller]")
{
    const juce::String notXml = "this is not xml at all";

    const auto result = sg::rewritePresetPathsForThisMachine (
        notXml, juce::File ("C:\\models"), juce::File ("C:\\irs"));

    CHECK (result == notXml);
}

TEST_CASE ("rewritePresetPathsForThisMachine leaves a preset with no namModelPath/irPath untouched", "[content][ContentInstaller]")
{
    const juce::String stateXml = "<PARAMETERS chainOrder=\"screamer,echoes,chamber\"><PARAM id=\"outputGain\" value=\"0.0\"/></PARAMETERS>";

    const auto rewritten = sg::rewritePresetPathsForThisMachine (
        stateXml, juce::File ("C:\\models"), juce::File ("C:\\irs"));

    const std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (rewritten));
    REQUIRE (xml != nullptr);
    CHECK_FALSE (xml->hasAttribute ("namModelPath"));
    CHECK_FALSE (xml->hasAttribute ("irPath"));
}
