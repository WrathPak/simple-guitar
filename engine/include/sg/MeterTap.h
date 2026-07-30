#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace sg
{

/**
    Lock-free stereo peak meter tap.

    The audio thread calls pushBlock() once per processed block; any other
    thread (typically a ~30-60Hz UI timer) calls getPeak() to read the most
    recent per-channel peak magnitude (linear, not dB). Both paths are
    realtime-safe: pushBlock() never locks or allocates, and communication
    is via std::atomic<float> only — no FIFO, no dynamic memory.

    This tap reports the *raw* peak of the most recent block; it does not
    apply hold/decay/ballistics. That's a UI-side concern for whoever
    consumes getPeak() on a timer.
*/
class MeterTap
{
public:
    static constexpr int maxChannels = 2;

    MeterTap() noexcept;

    /** Clears all channel peaks back to 0. Safe to call from any thread. */
    void reset() noexcept;

    /** Scans up to maxChannels of the given block for per-channel peak
        absolute magnitude and stores it atomically. Audio-thread only:
        no locks, no allocation. channelData must have at least numChannels
        valid pointers, each with at least numSamples valid samples. */
    void pushBlock (const float* const* channelData, int numChannels, int numSamples) noexcept;

    /** Thread-safe read of the most recent linear peak magnitude for a
        channel (0 or 1). Returns 0.0f for an out-of-range channel index.
        Safe to call from any thread. */
    float getPeak (int channel) const noexcept;

private:
    std::array<std::atomic<float>, (std::size_t) maxChannels> peaks;
};

} // namespace sg
