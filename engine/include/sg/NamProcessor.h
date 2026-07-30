#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>

namespace sg
{

/**
    Loads and runs Neural Amp Modeler (.nam) captures.

    NamProcessor sums a stereo (or mono) input to mono, runs it through the
    currently loaded NAM model, and writes the mono result to every output
    channel. With no model loaded it is a transparent passthrough.

    Threading model:
      - process() and reset() are audio-thread only: no locks, no allocation,
        no file I/O. They read the currently-active model through an atomic
        pointer and never mutate it.
      - requestLoad() is safe to call from the message thread (or any non-audio
        thread). It parses the .nam file and fully prepares a new model
        instance on a background thread, then publishes it by atomically
        swapping the active-model pointer. The audio thread picks up the swap
        on its next block and crossfades between the old and new model (or
        from silence, if none was loaded before) over a short, fixed window so
        the swap is click-free. The previous model is only ever destroyed on
        the background/loader thread, well after the crossfade has completed
        -- never on the audio thread.
      - requestLoad()'s completion callback is delivered asynchronously on the
        message thread (via juce::MessageManager::callAsync), so it is always
        safe to touch UI state, APVTS, etc. directly from inside it.
      - setNormalize() is a plain atomic flag, safe to call from any thread at
        any time.

    Sample-rate adaptation: a .nam model has an expected sample rate baked
    into its file (typically 48kHz). When the host/session sample rate
    differs, NamProcessor resamples into and back out of the model's native
    rate around the process() call, block by block. All resampling buffers are
    sized when a model is prepared (on the background thread); nothing is
    (re)allocated on the audio thread.
*/
class NamProcessor
{
public:
    NamProcessor();
    ~NamProcessor();

    /** Prepares the processor for a given session sample rate and maximum
        block size. Call before processing, and again whenever either changes.
        May allocate; do not call from the audio thread. */
    void prepare (double sampleRate, int maxBlockSize) noexcept;

    /** Processes a stereo (or mono) audio block in place. Audio-thread only:
        no locks, no allocation, no file access. Sums the input channels to
        mono, runs the currently active model (if any), and writes the result
        to every channel in the buffer. Passthrough when no model is loaded. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Resets audio-thread-local processing state (resampler history,
        in-flight crossfade position). Audio-thread safe: no locks, no
        allocation. Does not unload or rebuild the currently active model. */
    void reset() noexcept;

    /** Asynchronously loads a .nam file and prepares it for use.
        Thread-safe; intended to be called from the message thread. Parsing
        and DSP preparation happen on a background thread; the new model is
        then swapped in atomically and click-free (short equal-power
        crossfade). onDone is invoked exactly once, asynchronously on the
        message thread: (true, modelName) on success, or (false,
        errorMessage) on failure. onDone may be a null/empty
        std::function, in which case completion is simply not reported. */
    void requestLoad (const juce::String& namFilePath,
                       std::function<void (bool ok, juce::String errorOrName)> onDone);

    /** True once a model has been successfully loaded and swapped in. */
    bool isLoaded() const noexcept;

    /** The active model's name, taken from its .nam metadata if present,
        otherwise the file's stem. Empty if no model is loaded. */
    juce::String getModelName() const noexcept;

    /** Enables/disables NAM loudness normalization. Thread-safe (atomic
        store); no-op when the active model carries no loudness metadata. */
    void setNormalize (bool shouldNormalize) noexcept;

    /** The active model's expected sample rate in Hz, or 0 if none is
        loaded. */
    double getModelSampleRate() const noexcept;

private:
    struct LoadedModel;
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamProcessor)
};

} // namespace sg
