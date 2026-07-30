#pragma once

#include <array>
#include <atomic>
#include <cstdint>

/**
    Pure, JUCE-free helpers for the floor pedal chain order: packing a
    3-element permutation of pedal slot indices into a single uint32_t so it
    can live in one std::atomic<uint32_t> and be read on the audio thread
    with a single relaxed load -- no lock, no allocation, glitch-free
    reordering.

    Kept dependency-free and header-only (like ui/src/components/chainOrder.ts
    on the UI side) so the packing/permutation-validation logic is directly
    Catch2-testable without a JUCE host context -- see
    tests/app/ChainOrderTest.cpp. PluginProcessor.h/.cpp is the only consumer
    on the C++ side; it maps sg::PedalSlot <-> the schema's PedalId strings
    ("screamer"/"echoes"/"chamber") when talking to WebviewBridge.
*/
namespace sg
{

/** The three floor pedals, identified by a fixed index rather than string --
    matches schema/bridge.schema.json's PedalId enum order. */
enum class PedalSlot : std::uint8_t
{
    screamer = 0,
    echoes = 1,
    chamber = 2
};

constexpr int numPedalSlots = 3;

/** A left-to-right / signal order: order[i] is the pedal occupying position
    i (position 0 is first in the signal path). A valid order is always a
    permutation of {screamer, echoes, chamber}. */
using PedalOrder = std::array<PedalSlot, (std::size_t) numPedalSlots>;

/** The fixed template order pedals start in: screamer, echoes, chamber. */
constexpr PedalOrder defaultPedalOrder() noexcept
{
    return { PedalSlot::screamer, PedalSlot::echoes, PedalSlot::chamber };
}

/** True if `order` contains each of the three pedal slots exactly once. */
constexpr bool isValidPedalOrder (const PedalOrder& order) noexcept
{
    bool seen[numPedalSlots] = { false, false, false };

    for (auto slot : order)
    {
        const auto index = (std::size_t) slot;
        if (index >= (std::size_t) numPedalSlots)
            return false;
        if (seen[index])
            return false; // duplicate
        seen[index] = true;
    }

    return true;
}

/** Packs a pedal order into a single uint32_t: one byte per position, holding
    that position's PedalSlot value (position 0 in the low byte). Only valid
    for a well-formed permutation (see isValidPedalOrder) -- callers must
    validate before packing if the order didn't come from unpackPedalOrder()
    or defaultPedalOrder() already. */
constexpr std::uint32_t packPedalOrder (const PedalOrder& order) noexcept
{
    std::uint32_t packed = 0;

    for (int i = 0; i < numPedalSlots; ++i)
        packed |= (std::uint32_t) order[(std::size_t) i] << (8 * i);

    return packed;
}

/** Inverse of packPedalOrder(). Always returns a well-formed PedalOrder
    struct shape (three PedalSlot values 0..2 from the low three bytes), but
    does NOT itself guarantee the result is a valid permutation -- run it
    through isValidPedalOrder() if `packed` didn't necessarily come from
    packPedalOrder() on a valid order (e.g. a corrupt/foreign state blob). */
constexpr PedalOrder unpackPedalOrder (std::uint32_t packed) noexcept
{
    PedalOrder order {};

    for (int i = 0; i < numPedalSlots; ++i)
        order[(std::size_t) i] = (PedalSlot) ((packed >> (8 * i)) & 0xFFu);

    return order;
}

/** Atomically packs and stores `order` into `target`. No-op (returns false)
    if `order` isn't a valid permutation -- callers on the message thread use
    this to reject a malformed setChainOrder message without ever exposing a
    bad value to the audio thread. Realtime-safe: a single atomic store, no
    lock, no allocation, safe to call from the audio thread too if ever
    needed. */
inline bool storePedalOrder (std::atomic<std::uint32_t>& target, const PedalOrder& order) noexcept
{
    if (! isValidPedalOrder (order))
        return false;

    target.store (packPedalOrder (order), std::memory_order_relaxed);
    return true;
}

/** Atomically loads and unpacks the current order. Realtime-safe: a single
    relaxed atomic load, no lock, no allocation -- intended to be called once
    per audio block. */
inline PedalOrder loadPedalOrder (const std::atomic<std::uint32_t>& source) noexcept
{
    return unpackPedalOrder (source.load (std::memory_order_relaxed));
}

} // namespace sg
