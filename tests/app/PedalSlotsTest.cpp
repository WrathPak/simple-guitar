// Catch2 v3 tests for app/source/PedalSlots.{h,cpp}: the pure
// computeMovePermutation() helper backing PluginProcessor::movePedal(), and
// PedalSlotHost's runtime type-swap-under-continuous-processing behaviour
// (the actual thing the audio thread depends on being click-free).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "PedalSlots.h"

using sg::computeMovePermutation;
using sg::PedalType;

//==============================================================================
// computeMovePermutation() -- pure, JUCE-free, directly testable.

TEST_CASE ("computeMovePermutation is identity for from == to", "[PedalSlots]")
{
    for (int i = 0; i < sg::numSlots; ++i)
    {
        const auto perm = computeMovePermutation (i, i);
        for (int j = 0; j < sg::numSlots; ++j)
            CHECK (perm[(std::size_t) j] == j);
    }
}

TEST_CASE ("computeMovePermutation is identity for out-of-range indices", "[PedalSlots]")
{
    for (auto [from, to] : { std::pair { -1, 2 }, std::pair { 2, -1 }, std::pair { 6, 2 }, std::pair { 2, 6 } })
    {
        const auto perm = computeMovePermutation (from, to);
        for (int j = 0; j < sg::numSlots; ++j)
            CHECK (perm[(std::size_t) j] == j);
    }
}

TEST_CASE ("computeMovePermutation shifts everything between from and to left when from < to", "[PedalSlots]")
{
    // Move slot 1 to slot 4: slots 2,3,4 (old) shift left into 1,2,3;
    // slot 1's old content lands at slot 4; slots 0 and 5 are untouched.
    const auto perm = computeMovePermutation (1, 4);
    const sg::SlotPermutation expected { 0, 2, 3, 4, 1, 5 };
    CHECK (perm == expected);
}

TEST_CASE ("computeMovePermutation shifts everything between from and to right when from > to", "[PedalSlots]")
{
    // Move slot 4 to slot 1: slots 1,2,3 (old) shift right into 2,3,4;
    // slot 4's old content lands at slot 1; slots 0 and 5 are untouched.
    const auto perm = computeMovePermutation (4, 1);
    const sg::SlotPermutation expected { 0, 4, 1, 2, 3, 5 };
    CHECK (perm == expected);
}

TEST_CASE ("computeMovePermutation handles moving the first slot to the last", "[PedalSlots]")
{
    const auto perm = computeMovePermutation (0, 5);
    const sg::SlotPermutation expected { 1, 2, 3, 4, 5, 0 };
    CHECK (perm == expected);
}

TEST_CASE ("computeMovePermutation handles moving the last slot to the first", "[PedalSlots]")
{
    const auto perm = computeMovePermutation (5, 0);
    const sg::SlotPermutation expected { 5, 0, 1, 2, 3, 4 };
    CHECK (perm == expected);
}

TEST_CASE ("computeMovePermutation is always a valid permutation of 0..5", "[PedalSlots]")
{
    for (int from = 0; from < sg::numSlots; ++from)
    {
        for (int to = 0; to < sg::numSlots; ++to)
        {
            const auto perm = computeMovePermutation (from, to);
            bool seen[sg::numSlots] = {};
            for (int i = 0; i < sg::numSlots; ++i)
            {
                REQUIRE (perm[(std::size_t) i] >= 0);
                REQUIRE (perm[(std::size_t) i] < sg::numSlots);
                REQUIRE_FALSE (seen[(std::size_t) perm[(std::size_t) i]]);
                seen[(std::size_t) perm[(std::size_t) i]] = true;
            }
        }
    }
}

//==============================================================================
// PedalType / validity helpers.

TEST_CASE ("isValidPedalType accepts exactly 0..7", "[PedalSlots]")
{
    for (int i = 0; i < sg::numPedalTypes; ++i)
        CHECK (sg::isValidPedalType (i));

    CHECK_FALSE (sg::isValidPedalType (-1));
    CHECK_FALSE (sg::isValidPedalType (sg::numPedalTypes));
}

TEST_CASE ("isValidSlotIndex accepts exactly 0..5", "[PedalSlots]")
{
    for (int i = 0; i < sg::numSlots; ++i)
        CHECK (sg::isValidSlotIndex (i));

    CHECK_FALSE (sg::isValidSlotIndex (-1));
    CHECK_FALSE (sg::isValidSlotIndex (6));
}

TEST_CASE ("slotParamId builds the expected ids", "[PedalSlots]")
{
    CHECK (sg::slotParamId (0, "Type") == "slot0Type");
    CHECK (sg::slotParamId (5, "P4") == "slot5P4");
    CHECK (sg::slotParamId (2, "On") == "slot2On");
}

//==============================================================================
// createPedalInstance() -- every type builds a live, prepared instance.

TEST_CASE ("createPedalInstance never returns null for any PedalType", "[PedalSlots]")
{
    // PedalType::plugin builds the passthrough placeholder here -- the real
    // hosted instance only ever arrives via swapSlotInstance() (see
    // PedalSlots.h).
    const PedalType types[] = { PedalType::empty, PedalType::screamer, PedalType::echoes,
                                 PedalType::chamber, PedalType::squeeze, PedalType::wobble,
                                 PedalType::shiver, PedalType::plugin };

    for (auto type : types)
    {
        auto instance = sg::createPedalInstance (type, 44100.0, 512, 2);
        REQUIRE (instance != nullptr);

        // Should be safe to process immediately after construction (already prepared).
        juce::AudioBuffer<float> buffer (2, 64);
        buffer.clear();
        instance->setEnabled (true);
        for (int p = 0; p < sg::numParamsPerSlot; ++p)
            instance->setParam (p, 0.5f);
        instance->process (buffer); // should not crash/assert
    }
}

TEST_CASE ("only screamer-type instances report nonzero latency", "[PedalSlots]")
{
    const PedalType types[] = { PedalType::empty, PedalType::echoes, PedalType::chamber,
                                 PedalType::squeeze, PedalType::wobble, PedalType::shiver };

    for (auto type : types)
    {
        auto instance = sg::createPedalInstance (type, 44100.0, 512, 2);
        CHECK (instance->getLatencySamples() == 0);
    }

    auto screamer = sg::createPedalInstance (PedalType::screamer, 44100.0, 512, 2);
    CHECK (screamer->getLatencySamples() > 0);
}

//==============================================================================
// PedalSlotHost: type-swap under continuous processing must be click-free.

namespace
{
    // Runs `host` over `numBlocks` blocks of `blockSize` samples of a
    // continuous sine (mono, held across blocks so there's no seam of its
    // own), applying `on`/`params` to `slot` every block, and appends the
    // fully-processed output (channel 0) onto `outSamples`.
    void runBlocks (sg::PedalSlotHost& host, int slot, bool on, const std::array<float, sg::numParamsPerSlot>& params,
                     double sampleRate, int blockSize, int numBlocks, double& phase, std::vector<float>& outSamples)
    {
        constexpr double freqHz = 440.0;
        constexpr float amplitude = 0.5f;

        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (1, blockSize);

            for (int i = 0; i < blockSize; ++i)
            {
                buffer.setSample (0, i, amplitude * (float) std::sin (phase));
                phase += juce::MathConstants<double>::twoPi * freqHz / sampleRate;
            }

            host.processSlot (slot, on, params, buffer);

            for (int i = 0; i < blockSize; ++i)
                outSamples.push_back (buffer.getSample (0, i));
        }
    }
}

TEST_CASE ("PedalSlotHost type swap under continuous processing produces no click", "[PedalSlots]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;

    sg::PedalSlotHost host;
    std::array<sg::PedalType, sg::numSlots> types {};
    types.fill (PedalType::empty);
    host.prepare (sampleRate, blockSize, 1, types);

    const std::array<float, sg::numParamsPerSlot> params { 0.5f, 0.5f, 0.5f, 0.5f };

    std::vector<float> output;
    double phase = 0.0;

    // A few blocks of "empty" (bit-exact passthrough) first...
    runBlocks (host, 0, true, params, sampleRate, blockSize, 10, phase, output);

    // ...then swap slot 0 to "echoes" mid-stream (the audio keeps running
    // continuously the whole time -- this is the swap the audio thread must
    // never click on).
    auto retired = host.swapSlot (0, PedalType::echoes, sampleRate, blockSize, 1);
    REQUIRE (retired != nullptr);

    // ...and keep processing right through the crossfade and beyond.
    runBlocks (host, 0, true, params, sampleRate, blockSize, 10, phase, output);

    REQUIRE (output.size() > 2);

    // A click reads as an abnormally large sample-to-sample jump relative to
    // the smooth sine's own maximum slope (~2*pi*440/44100 * amplitude, a
    // little over 0.03 for amplitude 0.5); the crossfade should keep every
    // first difference comfortably below a small multiple of that, nowhere
    // close to the near-full-scale jump an actual glitch would produce.
    float maxAbsDelta = 0.0f;
    for (std::size_t i = 1; i < output.size(); ++i)
        maxAbsDelta = std::max (maxAbsDelta, std::abs (output[i] - output[i - 1]));

    constexpr float clickThreshold = 0.2f; // well above normal slope, well below a real glitch
    CHECK (maxAbsDelta < clickThreshold);
}

TEST_CASE ("PedalSlotHost keeps processing correctly after a slot swap (echoes runs, not silence)", "[PedalSlots]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;

    sg::PedalSlotHost host;
    std::array<sg::PedalType, sg::numSlots> types {};
    types.fill (PedalType::empty);
    host.prepare (sampleRate, blockSize, 1, types);

    const std::array<float, sg::numParamsPerSlot> params { 0.5f, 0.5f, 0.5f, 0.5f };

    std::vector<float> output;
    double phase = 0.0;

    runBlocks (host, 0, true, params, sampleRate, blockSize, 5, phase, output);

    // The retired instance MUST be held until canRetireSafely() clears it
    // (see PedalSlots.h's retire-safety contract). This line previously
    // discarded the returned unique_ptr, deleting the old instance while
    // the processing side was still about to crossfade FROM it -- a
    // use-after-free that segfaulted on macOS (whose allocator reuses/
    // poisons the freed block sooner) and only silently survived on
    // Windows.
    auto retired = host.swapSlot (0, PedalType::echoes, sampleRate, blockSize, 1);
    REQUIRE (retired != nullptr);
    CHECK_FALSE (host.canRetireSafely (0, retired.get())); // still observable: swap not yet picked up

    // Long enough to run well past the crossfade window (~10ms).
    runBlocks (host, 0, true, params, sampleRate, blockSize, 40, phase, output);

    // Fade long finished -> provably unobservable now.
    CHECK (host.canRetireSafely (0, retired.get()));

    // The tail (long after the crossfade has finished) should be non-silent
    // -- proof the new "echoes" instance is genuinely live and processing,
    // not stuck on a frozen/silent fade target.
    float tailPeak = 0.0f;
    for (std::size_t i = output.size() - blockSize; i < output.size(); ++i)
        tailPeak = std::max (tailPeak, std::abs (output[i]));

    CHECK (tailPeak > 0.05f);
}

//==============================================================================
// Retire-safety contract (see PedalSlots.h's retire-safety proof): a retired
// instance may only be deleted once canRetireSafely() proves the processing
// side can no longer observe it.

TEST_CASE ("canRetireSafely tracks a retired instance through pickup, fade, and completion", "[PedalSlots]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;

    sg::PedalSlotHost host;
    std::array<sg::PedalType, sg::numSlots> types {};
    types.fill (PedalType::empty);
    host.prepare (sampleRate, blockSize, 1, types);

    const std::array<float, sg::numParamsPerSlot> params { 0.5f, 0.5f, 0.5f, 0.5f };
    std::vector<float> output;
    double phase = 0.0;

    runBlocks (host, 0, true, params, sampleRate, blockSize, 2, phase, output);

    auto retired = host.swapSlot (0, PedalType::echoes, sampleRate, blockSize, 1);
    REQUIRE (retired != nullptr);

    // Not yet picked up: the processing side's current instance is still
    // the retired one -- deleting it now is exactly the use-after-free the
    // macOS CI segfault caught.
    CHECK_FALSE (host.canRetireSafely (0, retired.get()));

    // One block: swap picked up, crossfade in flight -- the retired
    // instance is the fade source, still observable.
    runBlocks (host, 0, true, params, sampleRate, blockSize, 1, phase, output);
    CHECK_FALSE (host.canRetireSafely (0, retired.get()));

    // Run well past the ~10ms fade: now provably unobservable.
    runBlocks (host, 0, true, params, sampleRate, blockSize, 10, phase, output);
    CHECK (host.canRetireSafely (0, retired.get()));

    // An unrelated pointer (a never-installed instance) is trivially safe.
    auto stranger = sg::createPedalInstance (PedalType::empty, sampleRate, blockSize, 1);
    CHECK (host.canRetireSafely (0, stranger.get()));
}

TEST_CASE ("canRetireSafely stays false while processing is stalled -- wall-clock time alone never frees", "[PedalSlots]")
{
    // The production bug this guards: a wall-clock grace period elapses
    // while the host has stopped calling processBlock; the audio thread
    // then resumes and picks up the swap, crossfading from an
    // already-deleted instance. canRetireSafely() must keep answering
    // false for as long as no processing has happened, no matter how much
    // time passes.
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;

    sg::PedalSlotHost host;
    std::array<sg::PedalType, sg::numSlots> types {};
    types.fill (PedalType::empty);
    host.prepare (sampleRate, blockSize, 1, types);

    auto retired = host.swapSlot (0, PedalType::echoes, sampleRate, blockSize, 1);
    REQUIRE (retired != nullptr);

    // No processSlot() calls at all after the swap -- "stalled host".
    for (int i = 0; i < 100; ++i)
        CHECK_FALSE (host.canRetireSafely (0, retired.get()));
}

TEST_CASE ("repeated rapid swaps under continuous processing stay click-free with contract-driven retirement", "[PedalSlots]")
{
    // Hammer the swap path the way the production GC drives it: keep
    // processing continuously, swap the slot's type every couple of blocks
    // (often mid-crossfade), and delete retired instances the moment
    // canRetireSafely() allows -- exercising pickup-while-fading,
    // never-observed retirees, and deferred deletion in one loop.
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;

    sg::PedalSlotHost host;
    std::array<sg::PedalType, sg::numSlots> types {};
    types.fill (PedalType::empty);
    host.prepare (sampleRate, blockSize, 1, types);

    const std::array<float, sg::numParamsPerSlot> params { 0.5f, 0.5f, 0.5f, 0.5f };
    const PedalType cycle[] = { PedalType::echoes, PedalType::empty, PedalType::chamber,
                                 PedalType::screamer, PedalType::empty, PedalType::wobble };

    std::vector<std::unique_ptr<sg::PedalInstance>> retired;
    std::vector<float> output;
    double phase = 0.0;

    for (int swap = 0; swap < 24; ++swap)
    {
        retired.push_back (host.swapSlot (0, cycle[(std::size_t) (swap % 6)], sampleRate, blockSize, 1));

        // A couple of blocks -- deliberately SHORTER than the ~10ms fade at
        // 44.1kHz (441 samples), so the next swap always lands mid-fade.
        runBlocks (host, 0, true, params, sampleRate, blockSize, 2, phase, output);

        // Production-style GC: free exactly those retirees the host proves
        // unobservable, keep the rest.
        retired.erase (std::remove_if (retired.begin(), retired.end(),
                           [&host] (const std::unique_ptr<sg::PedalInstance>& p)
                           { return host.canRetireSafely (0, p.get()); }),
                       retired.end());
    }

    // Drain: run well past the last fade, then everything must be
    // retirable.
    runBlocks (host, 0, true, params, sampleRate, blockSize, 20, phase, output);
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                       [&host] (const std::unique_ptr<sg::PedalInstance>& p)
                       { return host.canRetireSafely (0, p.get()); }),
                   retired.end());
    CHECK (retired.empty());

    // And the whole ride must have stayed click-free (same first-difference
    // bound as the single-swap test above).
    float maxAbsDelta = 0.0f;
    for (std::size_t i = 1; i < output.size(); ++i)
        maxAbsDelta = std::max (maxAbsDelta, std::abs (output[i] - output[i - 1]));

    CHECK (maxAbsDelta < 0.35f); // screamer/chamber legs are hotter than a bare sine; still nowhere near a glitch
}

TEST_CASE ("swapSlotInstance injects a prepared instance with the same crossfade/retire contract", "[PedalSlots]")
{
    // The hosted-plugin injection point (see PluginHosting.h): a
    // caller-built instance swapped in via swapSlotInstance() must behave
    // exactly like a swapSlot()-built one -- crossfaded in, old instance
    // retired under the canRetireSafely() contract.
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;

    struct GainHalfPedal final : sg::PedalInstance
    {
        void process (juce::AudioBuffer<float>& buffer) noexcept override { buffer.applyGain (0.5f); }
        void setEnabled (bool) noexcept override {}
        void setParam (int, float) noexcept override {}
    };

    sg::PedalSlotHost host;
    std::array<sg::PedalType, sg::numSlots> types {};
    types.fill (PedalType::empty);
    host.prepare (sampleRate, blockSize, 1, types);

    const std::array<float, sg::numParamsPerSlot> params { 0.5f, 0.5f, 0.5f, 0.5f };
    std::vector<float> output;
    double phase = 0.0;

    runBlocks (host, 0, true, params, sampleRate, blockSize, 5, phase, output);

    auto retired = host.swapSlotInstance (0, std::make_unique<GainHalfPedal>());
    REQUIRE (retired != nullptr);
    CHECK_FALSE (host.canRetireSafely (0, retired.get()));

    runBlocks (host, 0, true, params, sampleRate, blockSize, 40, phase, output);
    CHECK (host.canRetireSafely (0, retired.get()));

    // Well after the fade, the injected instance is genuinely processing:
    // the 0.5-amplitude sine comes out at ~0.25 peak.
    float tailPeak = 0.0f;
    for (std::size_t i = output.size() - blockSize; i < output.size(); ++i)
        tailPeak = std::max (tailPeak, std::abs (output[i]));

    CHECK (tailPeak > 0.2f);
    CHECK (tailPeak < 0.3f);

    // No click across the whole run either.
    float maxAbsDelta = 0.0f;
    for (std::size_t i = 1; i < output.size(); ++i)
        maxAbsDelta = std::max (maxAbsDelta, std::abs (output[i] - output[i - 1]));

    CHECK (maxAbsDelta < 0.2f);
}
