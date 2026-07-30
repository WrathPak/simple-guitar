#include "StateMigration.h"

#include "PedalSlots.h"

namespace sg
{

namespace
{
    constexpr const char* chainOrderAttr = "chainOrder";

    struct LegacyPedalDef
    {
        const char* onId;
        const char* p1Id;
        const char* p2Id;
        const char* p3Id;
        PedalType type;
    };

    constexpr LegacyPedalDef legacyScreamer { "screamerOn", "screamerDrive", "screamerTone", "screamerLevel", PedalType::screamer };
    constexpr LegacyPedalDef legacyEchoes { "echoesOn", "echoesTime", "echoesFeedback", "echoesMix", PedalType::echoes };
    constexpr LegacyPedalDef legacyChamber { "chamberOn", "chamberDecay", "chamberTone", "chamberMix", PedalType::chamber };

    const LegacyPedalDef* legacyDefForId (const juce::String& id) noexcept
    {
        if (id == "screamer") return &legacyScreamer;
        if (id == "echoes") return &legacyEchoes;
        if (id == "chamber") return &legacyChamber;
        return nullptr;
    }

    // The 12 legacy PARAM ids that must be stripped from a migrated tree.
    constexpr const char* legacyParamIds[] = {
        "screamerOn", "screamerDrive", "screamerTone", "screamerLevel",
        "echoesOn", "echoesTime", "echoesFeedback", "echoesMix",
        "chamberOn", "chamberDecay", "chamberTone", "chamberMix"
    };

    float getLegacyParamValue (const juce::XmlElement& root, const juce::String& id, float fallback)
    {
        for (auto* child : root.getChildIterator())
            if (child->hasTagName ("PARAM") && child->getStringAttribute ("id") == id)
                return (float) child->getDoubleAttribute ("value", (double) fallback);

        return fallback;
    }

    void addParamChild (juce::XmlElement& root, const juce::String& id, double value)
    {
        auto* param = root.createNewChildElement ("PARAM");
        param->setAttribute ("id", id);
        param->setAttribute ("value", value);
    }
}

bool isLegacyStateXml (const juce::XmlElement& root)
{
    return root.hasAttribute (chainOrderAttr);
}

std::unique_ptr<juce::XmlElement> migrateStateXmlIfNeeded (const juce::XmlElement& root)
{
    auto migrated = std::make_unique<juce::XmlElement> (root);

    if (! isLegacyStateXml (root))
        return migrated;

    // Parse the old chainOrder attribute ("screamer,echoes,chamber" in some
    // permutation) -- position i in this list becomes slot i.
    const auto chainOrderIds = juce::StringArray::fromTokens (root.getStringAttribute (chainOrderAttr), ",", "");

    // The migrated tree must look exactly like a fresh new-format tree to
    // whatever restores it next (apvts.replaceState()): drop chainOrder and
    // every legacy PARAM child first.
    migrated->removeAttribute (chainOrderAttr);

    for (auto* legacyId : legacyParamIds)
    {
        for (int i = migrated->getNumChildElements(); --i >= 0;)
        {
            auto* child = migrated->getChildElement (i);
            if (child->hasTagName ("PARAM") && child->getStringAttribute ("id") == juce::String (legacyId))
                migrated->removeChildElement (child, true);
        }
    }

    // Build the 6 slots: legacy chainOrder position i -> slot i; anything
    // beyond the (at most 3) legacy pedals -- or an entry that doesn't match
    // a known legacy pedal id -- comes out empty.
    for (int slot = 0; slot < numSlots; ++slot)
    {
        PedalType type = PedalType::empty;
        bool on = true;
        float p1 = 0.5f, p2 = 0.5f, p3 = 0.5f, p4 = 0.5f;

        if (slot < chainOrderIds.size())
        {
            if (const auto* def = legacyDefForId (chainOrderIds[slot]))
            {
                type = def->type;
                on = getLegacyParamValue (root, def->onId, 1.0f) >= 0.5f;
                p1 = getLegacyParamValue (root, def->p1Id, 0.5f);
                p2 = getLegacyParamValue (root, def->p2Id, 0.5f);
                p3 = getLegacyParamValue (root, def->p3Id, 0.5f);
                p4 = 0.5f; // legacy pedals only ever had 3 knobs
            }
        }

        addParamChild (*migrated, slotParamId (slot, "Type"), (double) (int) type);
        addParamChild (*migrated, slotParamId (slot, "On"), on ? 1.0 : 0.0);
        addParamChild (*migrated, slotParamId (slot, "P1"), (double) p1);
        addParamChild (*migrated, slotParamId (slot, "P2"), (double) p2);
        addParamChild (*migrated, slotParamId (slot, "P3"), (double) p3);
        addParamChild (*migrated, slotParamId (slot, "P4"), (double) p4);
    }

    return migrated;
}

} // namespace sg
