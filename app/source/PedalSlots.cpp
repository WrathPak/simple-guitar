#include "PedalSlots.h"

#include <cmath>

#include <sg/Chorus.h>
#include <sg/Compressor.h>
#include <sg/PlateReverb.h>
#include <sg/Screamer.h>
#include <sg/StereoDelay.h>
#include <sg/Tremolo.h>

namespace sg
{

namespace
{
    //==========================================================================
    // Concrete PedalInstance adapters, one per real pedal type, translating
    // the generic P1..P4 slot knobs onto each engine class's own named
    // setters (see the per-type P mapping in PluginProcessor.h's class-level
    // comment / the schema contract). Each wraps exactly one engine object;
    // process()/setEnabled()/setParam() just forward to it.

    struct EmptyPedal final : PedalInstance
    {
        void process (juce::AudioBuffer<float>&) noexcept override {}
        void setEnabled (bool) noexcept override {}
        void setParam (int, float) noexcept override {}
    };

    struct ScreamerPedal final : PedalInstance
    {
        Screamer screamer;

        void process (juce::AudioBuffer<float>& buffer) noexcept override { screamer.process (buffer); }
        void setEnabled (bool on) noexcept override { screamer.setEnabled (on); }

        void setParam (int index, float v01) noexcept override
        {
            switch (index)
            {
                case 0: screamer.setDrive (v01); break;
                case 1: screamer.setTone (v01); break;
                case 2: screamer.setLevel (v01); break;
                default: break; // P4 unused
            }
        }

        int getLatencySamples() const noexcept override { return screamer.getLatencySamples(); }
    };

    struct EchoesPedal final : PedalInstance
    {
        StereoDelay delay;

        void process (juce::AudioBuffer<float>& buffer) noexcept override { delay.process (buffer); }
        void setEnabled (bool on) noexcept override { delay.setEnabled (on); }

        void setParam (int index, float v01) noexcept override
        {
            switch (index)
            {
                case 0: delay.setTime (v01); break;
                case 1: delay.setFeedback (v01); break;
                case 2: delay.setMix (v01); break;
                default: break; // P4 unused
            }
        }
    };

    struct ChamberPedal final : PedalInstance
    {
        PlateReverb reverb;

        void process (juce::AudioBuffer<float>& buffer) noexcept override { reverb.process (buffer); }
        void setEnabled (bool on) noexcept override { reverb.setEnabled (on); }

        void setParam (int index, float v01) noexcept override
        {
            switch (index)
            {
                case 0: reverb.setDecay (v01); break;
                case 1: reverb.setTone (v01); break;
                case 2: reverb.setMix (v01); break;
                default: break; // P4 unused
            }
        }
    };

    struct SqueezePedal final : PedalInstance
    {
        Compressor compressor;

        void process (juce::AudioBuffer<float>& buffer) noexcept override { compressor.process (buffer); }
        void setEnabled (bool on) noexcept override { compressor.setEnabled (on); }

        void setParam (int index, float v01) noexcept override
        {
            switch (index)
            {
                case 0: compressor.setAmount (v01); break;
                case 1: compressor.setAttack (v01); break;
                case 2: compressor.setRelease (v01); break;
                case 3: compressor.setLevel (v01); break;
                default: break;
            }
        }
    };

    struct WobblePedal final : PedalInstance
    {
        Chorus chorus;

        void process (juce::AudioBuffer<float>& buffer) noexcept override { chorus.process (buffer); }
        void setEnabled (bool on) noexcept override { chorus.setEnabled (on); }

        void setParam (int index, float v01) noexcept override
        {
            switch (index)
            {
                case 0: chorus.setRate (v01); break;
                case 1: chorus.setDepth (v01); break;
                case 2: chorus.setMix (v01); break;
                default: break; // P4 unused
            }
        }
    };

    struct ShiverPedal final : PedalInstance
    {
        Tremolo tremolo;

        void process (juce::AudioBuffer<float>& buffer) noexcept override { tremolo.process (buffer); }
        void setEnabled (bool on) noexcept override { tremolo.setEnabled (on); }

        void setParam (int index, float v01) noexcept override
        {
            switch (index)
            {
                case 0: tremolo.setRate (v01); break;
                case 1: tremolo.setDepth (v01); break;
                case 2: tremolo.setShape (v01); break;
                default: break; // P4 unused
            }
        }
    };
} // namespace

std::unique_ptr<PedalInstance> createPedalInstance (PedalType type, double sampleRate, int maxBlockSize, int numChannels)
{
    switch (type)
    {
        case PedalType::screamer:
        {
            auto instance = std::make_unique<ScreamerPedal>();
            instance->screamer.prepare (sampleRate, maxBlockSize, numChannels);
            return instance;
        }

        case PedalType::echoes:
        {
            auto instance = std::make_unique<EchoesPedal>();
            instance->delay.prepare (sampleRate, maxBlockSize, numChannels);
            return instance;
        }

        case PedalType::chamber:
        {
            auto instance = std::make_unique<ChamberPedal>();
            instance->reverb.prepare (sampleRate, maxBlockSize, numChannels);
            return instance;
        }

        case PedalType::squeeze:
        {
            auto instance = std::make_unique<SqueezePedal>();
            instance->compressor.prepare (sampleRate, maxBlockSize, numChannels);
            return instance;
        }

        case PedalType::wobble:
        {
            auto instance = std::make_unique<WobblePedal>();
            instance->chorus.prepare (sampleRate, maxBlockSize, numChannels);
            return instance;
        }

        case PedalType::shiver:
        {
            auto instance = std::make_unique<ShiverPedal>();
            instance->tremolo.prepare (sampleRate, maxBlockSize, numChannels);
            return instance;
        }

        case PedalType::empty:
        case PedalType::plugin: // placeholder only -- the real hosted instance arrives later via swapSlotInstance() (see PedalSlots.h)
        default:
            return std::make_unique<EmptyPedal>();
    }
}

//==============================================================================
PedalSlotHost::PedalSlotHost() = default;

PedalSlotHost::~PedalSlotHost()
{
    for (auto& slot : slots)
        delete slot.active.load (std::memory_order_acquire);
}

void PedalSlotHost::prepare (double sampleRate, int maxBlockSize, int numChannels, const std::array<PedalType, numSlots>& types)
{
    const int crossfadeLength = juce::jmax (1, (int) std::round (sampleRate * crossfadeSeconds));

    for (int i = 0; i < numSlots; ++i)
    {
        auto& slot = slots[(std::size_t) i];

        auto newInstance = createPedalInstance (types[(std::size_t) i], sampleRate, maxBlockSize, numChannels);
        PedalInstance* published = newInstance.release();

        // Safe to delete directly here (no grace period, no crossfade):
        // prepare() is only ever called when processing is guaranteed not to
        // be running concurrently, same assumption every other engine
        // block's own prepare() makes throughout this codebase.
        PedalInstance* old = slot.active.exchange (published, std::memory_order_acq_rel);
        delete old;

        slot.audioThreadCurrent = published;
        slot.fadeFrom = nullptr;
        slot.crossfadeRemaining = 0;
        slot.crossfadeLength = crossfadeLength;

        // Reseed the retire-safety mirrors (see PedalSlots.h): processing is
        // guaranteed stopped during prepare(), so plain stores are fine.
        slot.publishedCurrent.store (published, std::memory_order_release);
        slot.publishedFadeFrom.store (nullptr, std::memory_order_release);

        slot.fadeFromScratch.setSize (numChannels, maxBlockSize);
        slot.fadeToScratch.setSize (numChannels, maxBlockSize);
    }
}

std::unique_ptr<PedalInstance> PedalSlotHost::swapSlot (int slotIndex, PedalType type, double sampleRate, int maxBlockSize, int numChannels)
{
    jassert (isValidSlotIndex (slotIndex));
    auto& slot = slots[(std::size_t) slotIndex];

    auto newInstance = createPedalInstance (type, sampleRate, maxBlockSize, numChannels);
    PedalInstance* published = newInstance.release();

    PedalInstance* old = slot.active.exchange (published, std::memory_order_acq_rel);
    return std::unique_ptr<PedalInstance> (old);
}

std::unique_ptr<PedalInstance> PedalSlotHost::swapSlotInstance (int slotIndex, std::unique_ptr<PedalInstance> newInstance)
{
    jassert (isValidSlotIndex (slotIndex));
    jassert (newInstance != nullptr); // hand in an empty/passthrough instance, never null

    auto& slot = slots[(std::size_t) slotIndex];

    PedalInstance* published = newInstance.release();
    PedalInstance* old = slot.active.exchange (published, std::memory_order_acq_rel);
    return std::unique_ptr<PedalInstance> (old);
}

void PedalSlotHost::processSlot (int slotIndex, bool on, const std::array<float, numParamsPerSlot>& params, juce::AudioBuffer<float>& buffer) noexcept
{
    jassert (isValidSlotIndex (slotIndex));
    auto& slot = slots[(std::size_t) slotIndex];

    PedalInstance* latest = slot.active.load (std::memory_order_acquire);

    if (latest != slot.audioThreadCurrent)
    {
        slot.fadeFrom = slot.audioThreadCurrent;
        slot.audioThreadCurrent = latest;
        slot.crossfadeRemaining = slot.crossfadeLength;

        // Publish the swap pickup for the message-thread retire-safety
        // check (see PedalSlots.h): from here until the fade completes,
        // BOTH pointers are in use on this thread. publishedFadeFrom must
        // be stored before publishedCurrent -- if the message thread
        // observed {current=new} before {fadeFrom=old} landed, it could
        // momentarily see the old instance in neither slot and free it
        // mid-fade.
        slot.publishedFadeFrom.store (slot.fadeFrom, std::memory_order_release);
        slot.publishedCurrent.store (latest, std::memory_order_release);
    }

    auto* current = slot.audioThreadCurrent;
    if (current == nullptr)
        return; // prepare() hasn't run yet -- shouldn't happen once it has; safe no-op.

    current->setEnabled (on);
    for (int i = 0; i < numParamsPerSlot; ++i)
        current->setParam (i, params[(std::size_t) i]);

    if (slot.crossfadeRemaining <= 0 || slot.fadeFrom == nullptr)
    {
        current->process (buffer);
        return;
    }

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    slot.fadeFromScratch.setSize (numChannels, numSamples, false, false, true);
    slot.fadeToScratch.setSize (numChannels, numSamples, false, false, true);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        slot.fadeFromScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);
        slot.fadeToScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);
    }

    // The retiring instance keeps running on its last-known params (frozen
    // for the ~10ms fade) -- only the surviving `current` instance gets the
    // fresh on/off + knob values applied above, same as NamProcessor doesn't
    // feed the outgoing model anything new either.
    slot.fadeFrom->process (slot.fadeFromScratch);
    current->process (slot.fadeToScratch);

    int remaining = slot.crossfadeRemaining;
    const int crossfadeLength = slot.crossfadeLength;

    for (int i = 0; i < numSamples; ++i)
    {
        if (remaining > 0)
        {
            const double t = 1.0 - (double) remaining / (double) crossfadeLength;
            const float gFrom = (float) std::cos (t * juce::MathConstants<double>::halfPi);
            const float gTo = (float) std::sin (t * juce::MathConstants<double>::halfPi);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                buffer.setSample (ch, i, slot.fadeFromScratch.getSample (ch, i) * gFrom
                                              + slot.fadeToScratch.getSample (ch, i) * gTo);
            }

            --remaining;
        }
        else
        {
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample (ch, i, slot.fadeToScratch.getSample (ch, i));
        }
    }

    slot.crossfadeRemaining = remaining;

    if (remaining <= 0)
    {
        // Fade complete: the retiring instance was read for the last time
        // above -- clear our own reference first, then publish the release
        // (see PedalSlots.h's retire-safety proof) so the message thread
        // may free it.
        slot.fadeFrom = nullptr;
        slot.publishedFadeFrom.store (nullptr, std::memory_order_release);
    }
}

bool PedalSlotHost::canRetireSafely (int slotIndex, const PedalInstance* retired) const noexcept
{
    if (! isValidSlotIndex (slotIndex))
        return true;

    const auto& slot = slots[(std::size_t) slotIndex];

    if (retired == slot.active.load (std::memory_order_acquire))
        return false; // still the published instance -- the audio thread could pick it up on its next block

    // Load order matters (see PedalSlots.h): publishedCurrent first. If we
    // observe the swap-pickup's current store, its acquire synchronizes
    // with the audio thread's (earlier, program-ordered) publishedFadeFrom
    // store, so the fadeFrom load below can't miss an in-flight fade
    // source.
    if (retired == slot.publishedCurrent.load (std::memory_order_acquire))
        return false; // the audio thread's live instance

    if (retired == slot.publishedFadeFrom.load (std::memory_order_acquire))
        return false; // mid-crossfade read source

    return true;
}

int PedalSlotHost::getReportedLatencySamples (int slotIndex, bool on) const noexcept
{
    if (! on)
        return 0;

    jassert (isValidSlotIndex (slotIndex));
    const auto& slot = slots[(std::size_t) slotIndex];

    if (auto* instance = slot.active.load (std::memory_order_acquire))
        return instance->getLatencySamples();

    return 0;
}

} // namespace sg
