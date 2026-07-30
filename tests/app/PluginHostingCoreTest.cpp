// Catch2 v3 tests for the pure halves of M3's plugin hosting:
// app/source/PluginScanCore.{h,cpp} (child-process scan protocol + plugins.xml
// cache format) and app/source/PluginSlotCore.{h,cpp} (hosted-slot wet/dry
// blend + state-blob base64). Everything here is pure string/XML/buffer math
// -- no plugin instances, no filesystem, no juce_audio_processors.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

#include "PluginScanCore.h"
#include "PluginSlotCore.h"

using Catch::Approx;

//==============================================================================
// Scan output protocol (child stdout/result-file contents).

TEST_CASE ("scan output round-trips through wrap/extract", "[PluginHosting]")
{
    juce::XmlElement result ("SCANRESULT");
    auto* child = result.createNewChildElement ("PLUGIN");
    child->setAttribute ("name", "Fake Fuzz");
    child->setAttribute ("uniqueId", 1234);

    const auto wrapped = sg::wrapScanResultXml (result.toString());
    const auto parsed = sg::extractScanResultXml (wrapped);

    REQUIRE (parsed != nullptr);
    CHECK (parsed->hasTagName ("SCANRESULT"));
    REQUIRE (parsed->getNumChildElements() == 1);
    CHECK (parsed->getChildElement (0)->getStringAttribute ("name") == "Fake Fuzz");
    CHECK (parsed->getChildElement (0)->getIntAttribute ("uniqueId") == 1234);
}

TEST_CASE ("extractScanResultXml survives garbage around the sentinels", "[PluginHosting]")
{
    // Plugins are free to spew to stdout during their own static init --
    // anything outside the sentinel markers must be ignored.
    juce::XmlElement result ("SCANRESULT");
    result.createNewChildElement ("PLUGIN")->setAttribute ("name", "Chatty");

    const auto output = "some plugin banner\nnoise 123\n"
                      + sg::wrapScanResultXml (result.toString())
                      + "\ntrailing noise";

    const auto parsed = sg::extractScanResultXml (output);
    REQUIRE (parsed != nullptr);
    CHECK (parsed->getNumChildElements() == 1);
}

TEST_CASE ("extractScanResultXml rejects crash-shaped output", "[PluginHosting]")
{
    // Missing markers, truncated payload, or non-XML between the markers all
    // read as "the child crashed" (the parent then blacklists the file).
    CHECK (sg::extractScanResultXml ("") == nullptr);
    CHECK (sg::extractScanResultXml ("random crash text") == nullptr);
    CHECK (sg::extractScanResultXml (juce::String (sg::scanOutputBeginMarker) + "\n<SCANRESULT>") == nullptr); // no end marker
    CHECK (sg::extractScanResultXml (juce::String (sg::scanOutputBeginMarker) + "\nnot xml at all\n" + sg::scanOutputEndMarker) == nullptr);
    CHECK (sg::extractScanResultXml (juce::String (sg::scanOutputBeginMarker) + "\n\n" + sg::scanOutputEndMarker) == nullptr); // empty payload

    // End marker before begin marker: nothing valid between them.
    CHECK (sg::extractScanResultXml (juce::String (sg::scanOutputEndMarker) + "\n" + sg::scanOutputBeginMarker) == nullptr);
}

TEST_CASE ("scan output with zero plugins is a valid (non-crash) result", "[PluginHosting]")
{
    // A clean child run that found nothing hostable must NOT read as a
    // crash -- the file just isn't a plugin.
    const auto wrapped = sg::wrapScanResultXml (juce::XmlElement ("SCANRESULT").toString());
    const auto parsed = sg::extractScanResultXml (wrapped);

    REQUIRE (parsed != nullptr);
    CHECK (parsed->hasTagName ("SCANRESULT"));
    CHECK (parsed->getNumChildElements() == 0);
}

//==============================================================================
// plugins.xml cache format (list + blacklist + folders + crash records).

TEST_CASE ("plugin cache XML round-trips folders and crash records", "[PluginHosting]")
{
    juce::XmlElement knownPlugins ("KNOWNPLUGINS");
    knownPlugins.createNewChildElement ("PLUGIN")->setAttribute ("name", "Fake Verb");
    knownPlugins.createNewChildElement ("BLACKLISTED")->setAttribute ("id", "C:\\bad\\Crashy.vst3");

    juce::StringArray folders { "C:\\Extra\\VST3", "D:\\More Plugins" };
    std::vector<sg::CrashedPluginRecord> crashed { { "Crashy", "C:\\bad\\Crashy.vst3" } };

    const auto cache = sg::createPluginCacheXml (&knownPlugins, folders, crashed);
    REQUIRE (cache != nullptr);

    // Serialize -> reparse, as the real save/load does.
    const auto reparsed = juce::XmlDocument::parse (cache->toString());
    REQUIRE (reparsed != nullptr);

    sg::PluginCacheContents contents;
    REQUIRE (sg::parsePluginCacheXml (*reparsed, contents));

    REQUIRE (contents.knownPluginsXml != nullptr);
    CHECK (contents.knownPluginsXml->hasTagName ("KNOWNPLUGINS"));
    CHECK (contents.knownPluginsXml->getNumChildElements() == 2); // plugin + blacklist entry pass through verbatim

    CHECK (contents.extraScanFolders == folders);

    REQUIRE (contents.crashed.size() == 1);
    CHECK (contents.crashed[0].name == "Crashy");
    CHECK (contents.crashed[0].path == "C:\\bad\\Crashy.vst3");
}

TEST_CASE ("plugin cache XML with no scanned list yet still round-trips", "[PluginHosting]")
{
    const auto cache = sg::createPluginCacheXml (nullptr, {}, {});

    sg::PluginCacheContents contents;
    REQUIRE (sg::parsePluginCacheXml (*cache, contents));

    CHECK (contents.knownPluginsXml == nullptr);
    CHECK (contents.extraScanFolders.isEmpty());
    CHECK (contents.crashed.empty());
}

TEST_CASE ("parsePluginCacheXml rejects a wrong root and ignores unknown children", "[PluginHosting]")
{
    juce::XmlElement wrongRoot ("NOTPLUGINS");
    sg::PluginCacheContents contents;
    CHECK_FALSE (sg::parsePluginCacheXml (wrongRoot, contents));

    juce::XmlElement root (sg::pluginCacheRootTag);
    root.createNewChildElement ("FUTURE_THING")->setAttribute ("x", 1); // forward compatibility
    root.createNewChildElement ("FOLDER")->setAttribute ("path", "C:\\F");
    CHECK (sg::parsePluginCacheXml (root, contents));
    CHECK (contents.extraScanFolders == juce::StringArray { "C:\\F" });
}

//==============================================================================
// Equal-power wet/dry blend (the hosted slot's P1).

TEST_CASE ("equalPowerWetDry endpoints and midpoint", "[PluginHosting]")
{
    const auto dry = sg::equalPowerWetDry (0.0f);
    CHECK (dry.wet == Approx (0.0f).margin (1.0e-6));
    CHECK (dry.dry == Approx (1.0f).margin (1.0e-6));

    const auto wet = sg::equalPowerWetDry (1.0f);
    CHECK (wet.wet == Approx (1.0f).margin (1.0e-6));
    CHECK (wet.dry == Approx (0.0f).margin (1.0e-6));

    const auto mid = sg::equalPowerWetDry (0.5f);
    CHECK (mid.wet == Approx (std::sqrt (0.5f)).margin (1.0e-6));
    CHECK (mid.dry == Approx (std::sqrt (0.5f)).margin (1.0e-6));
}

TEST_CASE ("equalPowerWetDry is constant-power across the whole range and clamps out-of-range input", "[PluginHosting]")
{
    for (float mix = 0.0f; mix <= 1.0f; mix += 0.05f)
    {
        const auto gains = sg::equalPowerWetDry (mix);
        CHECK (gains.wet * gains.wet + gains.dry * gains.dry == Approx (1.0f).margin (1.0e-5));
    }

    CHECK (sg::equalPowerWetDry (-1.0f).wet == Approx (0.0f).margin (1.0e-6));
    CHECK (sg::equalPowerWetDry (2.0f).dry == Approx (0.0f).margin (1.0e-6));
}

TEST_CASE ("blendWetDryInPlace mixes the processed and dry signals with equal-power gains", "[PluginHosting]")
{
    constexpr int numSamples = 64;

    // dry = constant 1.0; "wet" (already-processed) = constant 0.5.
    juce::AudioBuffer<float> wet (2, numSamples), dry (2, numSamples);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < numSamples; ++i)
        {
            wet.setSample (ch, i, 0.5f);
            dry.setSample (ch, i, 1.0f);
        }

    SECTION ("fully dry: output == dry input, plugin inaudible")
    {
        sg::blendWetDryInPlace (wet, dry, 0.0f);
        for (int ch = 0; ch < 2; ++ch)
            CHECK (wet.getSample (ch, 20) == Approx (1.0f).margin (1.0e-6));
    }

    SECTION ("fully wet: output == the processed signal")
    {
        sg::blendWetDryInPlace (wet, dry, 1.0f);
        for (int ch = 0; ch < 2; ++ch)
            CHECK (wet.getSample (ch, 20) == Approx (0.5f).margin (1.0e-6));
    }

    SECTION ("midpoint: equal-power sum of both")
    {
        sg::blendWetDryInPlace (wet, dry, 0.5f);
        const float expected = std::sqrt (0.5f) * 0.5f + std::sqrt (0.5f) * 1.0f;
        for (int ch = 0; ch < 2; ++ch)
            CHECK (wet.getSample (ch, 20) == Approx (expected).margin (1.0e-5));
    }
}

//==============================================================================
// State-blob base64 (slot{N}PluginState property encoding).

TEST_CASE ("state blob round-trips through base64", "[PluginHosting]")
{
    juce::MemoryBlock blob;
    for (int i = 0; i < 300; ++i) // includes every byte value + non-trivial length
    {
        const auto byte = (char) (i % 256);
        blob.append (&byte, 1);
    }

    const auto encoded = sg::stateBlobToBase64 (blob);
    CHECK (encoded.isNotEmpty());

    juce::MemoryBlock decoded;
    REQUIRE (sg::base64ToStateBlob (encoded, decoded));
    CHECK (decoded == blob);
}

TEST_CASE ("empty state blob encodes to empty and back", "[PluginHosting]")
{
    CHECK (sg::stateBlobToBase64 ({}).isEmpty());

    juce::MemoryBlock decoded;
    decoded.append ("x", 1);
    REQUIRE (sg::base64ToStateBlob ({}, decoded));
    CHECK (decoded.getSize() == 0);
}

TEST_CASE ("base64ToStateBlob rejects malformed text and leaves the output untouched", "[PluginHosting]")
{
    juce::MemoryBlock untouched;
    untouched.append ("keep", 4);

    CHECK_FALSE (sg::base64ToStateBlob ("!!!not base64!!!", untouched));
    CHECK (untouched.getSize() == 4);
}
