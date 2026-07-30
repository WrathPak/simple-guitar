// Catch2 v3 tests for app/source/StateMigration.{h,cpp}: migrating a
// pre-slot-architecture APVTS state (fixed screamer/echoes/chamber params +
// chainOrder attribute) into the current 6-generic-slot shape
// (slot0Type..slot5P4). Pure XML manipulation -- no filesystem, no
// AudioProcessor -- so this fixture builds the legacy XML text by hand,
// matching exactly what M2's old PluginProcessor used to write (and what
// the pre-slot content/presets/*.sgpreset files on disk actually contain).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_core/juce_core.h>

#include "PedalSlots.h"
#include "StateMigration.h"

using Catch::Approx;

namespace
{
    // Mirrors a real legacy .sgpreset's embedded state XML (see e.g. the
    // pre-migration content/presets/*.sgpreset files): root "PARAMETERS"
    // with namModelPath/irPath/chainOrder attributes and one <PARAM id=".."
    // value=".."/> per of the (then-)24 known param ids.
    juce::String legacyStateXml (const juce::String& chainOrder)
    {
        return juce::String (R"(<PARAMETERS namModelPath="brit crunch.nam" irPath="warm angled.wav" chainOrder=")")
             + chainOrder + R"(">
  <PARAM id="outputGain" value="0.0"/>
  <PARAM id="gateOn" value="1.0"/>
  <PARAM id="gateThresholdDb" value="-60.0"/>
  <PARAM id="ampInputDb" value="4.0"/>
  <PARAM id="ampBassDb" value="0.0"/>
  <PARAM id="ampMidDb" value="2.0"/>
  <PARAM id="ampTrebleDb" value="0.0"/>
  <PARAM id="ampPresenceDb" value="0.0"/>
  <PARAM id="namNormalize" value="1.0"/>
  <PARAM id="cabOn" value="1.0"/>
  <PARAM id="cabLowCutHz" value="70.0"/>
  <PARAM id="cabHighCutHz" value="7500.0"/>
  <PARAM id="screamerOn" value="0.0"/>
  <PARAM id="screamerDrive" value="0.5"/>
  <PARAM id="screamerTone" value="0.5"/>
  <PARAM id="screamerLevel" value="0.7"/>
  <PARAM id="echoesOn" value="1.0"/>
  <PARAM id="echoesTime" value="0.3"/>
  <PARAM id="echoesFeedback" value="0.2"/>
  <PARAM id="echoesMix" value="0.15"/>
  <PARAM id="chamberOn" value="1.0"/>
  <PARAM id="chamberDecay" value="0.35"/>
  <PARAM id="chamberTone" value="0.45"/>
  <PARAM id="chamberMix" value="0.18"/>
</PARAMETERS>)";
    }

    constexpr const char* legacyParamIds[] = {
        "screamerOn", "screamerDrive", "screamerTone", "screamerLevel",
        "echoesOn", "echoesTime", "echoesFeedback", "echoesMix",
        "chamberOn", "chamberDecay", "chamberTone", "chamberMix"
    };

    double paramValue (const juce::XmlElement& root, const juce::String& id)
    {
        for (auto* child : root.getChildIterator())
            if (child->hasTagName ("PARAM") && child->getStringAttribute ("id") == id)
                return child->getDoubleAttribute ("value");

        FAIL ("missing PARAM id: " << id);
        return 0.0;
    }

    int countParamChildren (const juce::XmlElement& root, const juce::String& id)
    {
        int count = 0;
        for (auto* child : root.getChildIterator())
            if (child->hasTagName ("PARAM") && child->getStringAttribute ("id") == id)
                ++count;
        return count;
    }
}

TEST_CASE ("isLegacyStateXml detects the chainOrder attribute", "[StateMigration]")
{
    const std::unique_ptr<juce::XmlElement> legacy (juce::XmlDocument::parse (legacyStateXml ("screamer,echoes,chamber")));
    REQUIRE (legacy != nullptr);
    CHECK (sg::isLegacyStateXml (*legacy));
}

TEST_CASE ("isLegacyStateXml is false for a current-format tree", "[StateMigration]")
{
    juce::XmlElement root ("PARAMETERS");
    root.setAttribute ("namModelPath", "brit crunch.nam");
    root.setAttribute ("irPath", "warm angled.wav");
    CHECK_FALSE (sg::isLegacyStateXml (root));
}

TEST_CASE ("migrateStateXmlIfNeeded passes a current-format tree through unchanged", "[StateMigration]")
{
    juce::XmlElement root ("PARAMETERS");
    root.setAttribute ("namModelPath", "brit crunch.nam");
    auto* param = root.createNewChildElement ("PARAM");
    param->setAttribute ("id", "slot0Type");
    param->setAttribute ("value", 1.0);

    const auto migrated = sg::migrateStateXmlIfNeeded (root);
    REQUIRE (migrated != nullptr);
    CHECK (migrated->isEquivalentTo (&root, true));
}

TEST_CASE ("migrateStateXmlIfNeeded maps chainOrder position -> slot index and carries values across", "[StateMigration]")
{
    const std::unique_ptr<juce::XmlElement> legacy (juce::XmlDocument::parse (legacyStateXml ("screamer,echoes,chamber")));
    REQUIRE (legacy != nullptr);

    const auto migrated = sg::migrateStateXmlIfNeeded (*legacy);
    REQUIRE (migrated != nullptr);

    // chainOrder attribute and every legacy PARAM id must be gone.
    CHECK_FALSE (migrated->hasAttribute ("chainOrder"));
    for (auto* legacyId : legacyParamIds)
        CHECK (countParamChildren (*migrated, legacyId) == 0);

    // Non-pedal params and the model/IR paths are untouched by migration.
    CHECK (migrated->getStringAttribute ("namModelPath") == "brit crunch.nam");
    CHECK (migrated->getStringAttribute ("irPath") == "warm angled.wav");
    CHECK (paramValue (*migrated, "outputGain") == 0.0);
    CHECK (paramValue (*migrated, "ampInputDb") == 4.0);

    // slot0 = screamer (position 0 of chainOrder): type 1, off, drive/tone/level -> P1/P2/P3.
    CHECK (paramValue (*migrated, "slot0Type") == (double) (int) sg::PedalType::screamer);
    CHECK (paramValue (*migrated, "slot0On") == 0.0);
    // Values sourced from the legacy XML pass through a float atomic inside
    // migrateStateXmlIfNeeded() (slot P1-4 are AudioParameterFloat, a plain
    // 0..1 real range -- same narrowing every other continuous param in
    // this codebase goes through), so a non-power-of-two decimal like 0.7
    // isn't bit-identical to the double literal below; Approx tolerates
    // that float/double rounding without hiding an actual migration bug.
    CHECK (paramValue (*migrated, "slot0P1") == Approx (0.5)); // drive
    CHECK (paramValue (*migrated, "slot0P2") == Approx (0.5)); // tone
    CHECK (paramValue (*migrated, "slot0P3") == Approx (0.7)); // level
    CHECK (paramValue (*migrated, "slot0P4") == Approx (0.5)); // unused, flat default

    // slot1 = echoes (position 1): type 2, on, time/feedback/mix -> P1/P2/P3.
    CHECK (paramValue (*migrated, "slot1Type") == (double) (int) sg::PedalType::echoes);
    CHECK (paramValue (*migrated, "slot1On") == 1.0);
    CHECK (paramValue (*migrated, "slot1P1") == Approx (0.3)); // time
    CHECK (paramValue (*migrated, "slot1P2") == Approx (0.2)); // feedback
    CHECK (paramValue (*migrated, "slot1P3") == Approx (0.15)); // mix
    CHECK (paramValue (*migrated, "slot1P4") == Approx (0.5));

    // slot2 = chamber (position 2): type 3, on, decay/tone/mix -> P1/P2/P3.
    CHECK (paramValue (*migrated, "slot2Type") == (double) (int) sg::PedalType::chamber);
    CHECK (paramValue (*migrated, "slot2On") == 1.0);
    CHECK (paramValue (*migrated, "slot2P1") == Approx (0.35)); // decay
    CHECK (paramValue (*migrated, "slot2P2") == Approx (0.45)); // tone
    CHECK (paramValue (*migrated, "slot2P3") == Approx (0.18)); // mix
    CHECK (paramValue (*migrated, "slot2P4") == Approx (0.5));

    // slots 3-5: absent from the legacy 3-pedal chain -> empty, flat defaults.
    for (int slot = 3; slot < sg::numSlots; ++slot)
    {
        const auto prefix = "slot" + juce::String (slot);
        CHECK (paramValue (*migrated, prefix + "Type") == (double) (int) sg::PedalType::empty);
        CHECK (paramValue (*migrated, prefix + "On") == 1.0);
        CHECK (paramValue (*migrated, prefix + "P1") == 0.5);
        CHECK (paramValue (*migrated, prefix + "P2") == 0.5);
        CHECK (paramValue (*migrated, prefix + "P3") == 0.5);
        CHECK (paramValue (*migrated, prefix + "P4") == 0.5);
    }

    // Exactly one PARAM child per slot param id (36 total) -- no duplicates
    // left behind from the legacy children.
    for (int slot = 0; slot < sg::numSlots; ++slot)
    {
        CHECK (countParamChildren (*migrated, sg::slotParamId (slot, "Type")) == 1);
        CHECK (countParamChildren (*migrated, sg::slotParamId (slot, "On")) == 1);
        for (int p = 1; p <= sg::numParamsPerSlot; ++p)
            CHECK (countParamChildren (*migrated, sg::slotParamId (slot, "P" + juce::String (p))) == 1);
    }
}

TEST_CASE ("migrateStateXmlIfNeeded respects a permuted chainOrder", "[StateMigration]")
{
    const std::unique_ptr<juce::XmlElement> legacy (juce::XmlDocument::parse (legacyStateXml ("chamber,screamer,echoes")));
    REQUIRE (legacy != nullptr);

    const auto migrated = sg::migrateStateXmlIfNeeded (*legacy);
    REQUIRE (migrated != nullptr);

    CHECK (paramValue (*migrated, "slot0Type") == (double) (int) sg::PedalType::chamber);
    CHECK (paramValue (*migrated, "slot1Type") == (double) (int) sg::PedalType::screamer);
    CHECK (paramValue (*migrated, "slot2Type") == (double) (int) sg::PedalType::echoes);
}
