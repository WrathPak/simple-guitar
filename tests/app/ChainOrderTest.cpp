// Catch2 v3 tests for the pedal chain-order packing/permutation-validation
// helpers (app/source/ChainOrder.h). These are pure, JUCE-free, header-only
// functions -- deliberately factored out of PluginProcessor so the
// order-packing logic (the thing the audio thread actually reads once per
// block) is unit-testable without a full JUCE host/processor context.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>

#include "ChainOrder.h"

using sg::PedalOrder;
using sg::PedalSlot;

TEST_CASE ("sg::defaultPedalOrder is screamer, echoes, chamber and is valid", "[ChainOrder]")
{
    constexpr auto order = sg::defaultPedalOrder();
    CHECK (order[0] == PedalSlot::screamer);
    CHECK (order[1] == PedalSlot::echoes);
    CHECK (order[2] == PedalSlot::chamber);
    CHECK (sg::isValidPedalOrder (order));
}

TEST_CASE ("sg::isValidPedalOrder accepts every permutation of the three slots", "[ChainOrder]")
{
    const PedalOrder permutations[] = {
        { PedalSlot::screamer, PedalSlot::echoes, PedalSlot::chamber },
        { PedalSlot::screamer, PedalSlot::chamber, PedalSlot::echoes },
        { PedalSlot::echoes, PedalSlot::screamer, PedalSlot::chamber },
        { PedalSlot::echoes, PedalSlot::chamber, PedalSlot::screamer },
        { PedalSlot::chamber, PedalSlot::screamer, PedalSlot::echoes },
        { PedalSlot::chamber, PedalSlot::echoes, PedalSlot::screamer },
    };

    for (const auto& order : permutations)
        CHECK (sg::isValidPedalOrder (order));
}

TEST_CASE ("sg::isValidPedalOrder rejects a duplicate slot", "[ChainOrder]")
{
    const PedalOrder order { PedalSlot::screamer, PedalSlot::screamer, PedalSlot::chamber };
    CHECK_FALSE (sg::isValidPedalOrder (order));
}

TEST_CASE ("sg::isValidPedalOrder rejects an out-of-range slot value", "[ChainOrder]")
{
    PedalOrder order = sg::defaultPedalOrder();
    order[1] = (PedalSlot) 99;
    CHECK_FALSE (sg::isValidPedalOrder (order));
}

TEST_CASE ("sg::packPedalOrder / unpackPedalOrder round-trip every permutation", "[ChainOrder]")
{
    const PedalOrder permutations[] = {
        { PedalSlot::screamer, PedalSlot::echoes, PedalSlot::chamber },
        { PedalSlot::screamer, PedalSlot::chamber, PedalSlot::echoes },
        { PedalSlot::echoes, PedalSlot::screamer, PedalSlot::chamber },
        { PedalSlot::echoes, PedalSlot::chamber, PedalSlot::screamer },
        { PedalSlot::chamber, PedalSlot::screamer, PedalSlot::echoes },
        { PedalSlot::chamber, PedalSlot::echoes, PedalSlot::screamer },
    };

    for (const auto& order : permutations)
    {
        const auto packed = sg::packPedalOrder (order);
        const auto roundTripped = sg::unpackPedalOrder (packed);
        CHECK (roundTripped == order);
    }
}

TEST_CASE ("sg::packPedalOrder packs one byte per position, position 0 in the low byte", "[ChainOrder]")
{
    const PedalOrder order { PedalSlot::chamber, PedalSlot::screamer, PedalSlot::echoes };
    const auto packed = sg::packPedalOrder (order);

    CHECK ((packed & 0xFFu) == (std::uint32_t) PedalSlot::chamber);
    CHECK (((packed >> 8) & 0xFFu) == (std::uint32_t) PedalSlot::screamer);
    CHECK (((packed >> 16) & 0xFFu) == (std::uint32_t) PedalSlot::echoes);
    CHECK ((packed >> 24) == 0u);
}

TEST_CASE ("sg::storePedalOrder / loadPedalOrder round-trip through an atomic", "[ChainOrder]")
{
    std::atomic<std::uint32_t> atomicOrder { sg::packPedalOrder (sg::defaultPedalOrder()) };

    const PedalOrder next { PedalSlot::chamber, PedalSlot::echoes, PedalSlot::screamer };
    const bool ok = sg::storePedalOrder (atomicOrder, next);

    CHECK (ok);
    CHECK (sg::loadPedalOrder (atomicOrder) == next);
}

TEST_CASE ("sg::storePedalOrder rejects an invalid permutation and leaves the atomic untouched", "[ChainOrder]")
{
    const auto initial = sg::packPedalOrder (sg::defaultPedalOrder());
    std::atomic<std::uint32_t> atomicOrder { initial };

    const PedalOrder invalid { PedalSlot::screamer, PedalSlot::screamer, PedalSlot::chamber };
    const bool ok = sg::storePedalOrder (atomicOrder, invalid);

    CHECK_FALSE (ok);
    CHECK (atomicOrder.load() == initial);
}
