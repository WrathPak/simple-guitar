#pragma once

#include <memory>

#include <juce_core/juce_core.h>

/**
    Migration from the pre-slot-architecture APVTS state format (a fixed
    screamer/echoes/chamber trio plus a chainOrder attribute) to the current
    6-generic-slot format (slot0Type..slot5P4). Both
    SimpleGuitarAudioProcessor::setStateInformation() (DAW session restore)
    and PresetStore-driven preset loads must accept either format
    transparently -- a legacy .sgpreset or DAW session saved before the slot
    rework should still load correctly, with its pedals landing in slots 0-2
    in their old chainOrder order and slots 3-5 coming out empty.

    Pure XML manipulation, no filesystem access -- directly Catch2-testable,
    same spirit as ContentInstaller.h's rewritePresetPathsForThisMachine().
    Operates on the plain APVTS state XML tree (root tag "PARAMETERS", the
    same tree apvts.copyState().createXml() produces and
    apvts.replaceState (juce::ValueTree::fromXml (...)) consumes) -- callers
    run this once, right after parsing that XML and before handing it to
    apvts.replaceState().
*/
namespace sg
{

/** True if `root` is in the OLD (pre-slot) format. Detected by the presence
    of the chainOrder attribute alone -- it only ever existed in the old
    format, and always travelled together with the 12 legacy pedal PARAM
    ids (screamerOn/screamerDrive/.../chamberMix), so checking for it is
    sufficient. */
bool isLegacyStateXml (const juce::XmlElement& root);

/** Returns a migrated clone of `root` if it's in the legacy format (see
    isLegacyStateXml()): the chainOrder attribute and the 12 legacy pedal
    PARAM children are removed, and 36 new slot PARAM children (slot0Type..
    slot5P4) are added in their place. Legacy chainOrder position i becomes
    slot i (slot index IS signal order now) -- slots beyond the old chain's
    (at most) 3 pedals, or a chainOrder entry that doesn't match a known
    legacy pedal id, come out empty (type = 0, on = true, P1..P4 = 0.5,
    the same flat defaults used everywhere else for an empty/fresh slot).
    If `root` is not in the legacy format, returns an unmodified clone
    instead, so callers can always just use the result unconditionally
    rather than branching on whether a migration actually happened. */
std::unique_ptr<juce::XmlElement> migrateStateXmlIfNeeded (const juce::XmlElement& root);

} // namespace sg
