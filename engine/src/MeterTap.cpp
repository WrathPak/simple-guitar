#include "sg/MeterTap.h"

#include <cmath>

namespace sg
{

MeterTap::MeterTap() noexcept
{
    reset();
}

void MeterTap::reset() noexcept
{
    for (auto& p : peaks)
        p.store (0.0f, std::memory_order_relaxed);
}

void MeterTap::pushBlock (const float* const* channelData, int numChannels, int numSamples) noexcept
{
    const int channelsToScan = numChannels < maxChannels ? numChannels : maxChannels;

    for (int ch = 0; ch < channelsToScan; ++ch)
    {
        const float* data = channelData[ch];
        float peak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float sample = std::abs (data[i]);

            if (sample > peak)
                peak = sample;
        }

        peaks[(std::size_t) ch].store (peak, std::memory_order_release);
    }
}

float MeterTap::getPeak (int channel) const noexcept
{
    if (channel < 0 || channel >= maxChannels)
        return 0.0f;

    return peaks[(std::size_t) channel].load (std::memory_order_acquire);
}

} // namespace sg
