#include "sg/NamProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include <NAM/dsp.h>
#include <NAM/get_dsp.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace sg
{

namespace
{
    // Fixed loudness normalization target, matching the convention NAM
    // captures are typically calibrated against. Only applied when the
    // loaded model actually carries loudness metadata; otherwise
    // setNormalize() is a no-op.
    constexpr double kTargetLoudnessDb = -18.0;
}

//==============================================================================
// A single fully-prepared model instance: the DSP object plus everything
// needed to run it from process() without allocating -- resamplers (reset,
// but otherwise stateful/streaming across blocks) and pre-sized scratch
// buffers. Constructed and prepared entirely on the loader thread; once
// published via NamProcessor::Impl::activeModel, treated as immutable by the
// audio thread and destroyed only on a loader thread.
struct NamProcessor::LoadedModel
{
    std::unique_ptr<nam::DSP> dsp;
    juce::String name;
    double modelSampleRate = 0.0;
    bool needsResampling = false;
    bool hasLoudness = false;
    float normalizationGain = 1.0f;
    int maxModelFrames = 0;

    juce::LagrangeInterpolator inputResampler;  // session-rate -> model-rate
    juce::LagrangeInterpolator outputResampler; // model-rate -> session-rate

    std::vector<float> modelRateInputScratch;   // resampled mono input, model rate
    std::vector<float> modelRateOutputScratch;  // model output, model rate (float)
    std::vector<double> namInputScratch;        // NAM_SAMPLE (double) input
    std::vector<double> namOutputScratch;       // NAM_SAMPLE (double) output

    // Sizes every buffer and resets both resamplers for a given session
    // sample rate / max block size. Loader-thread only; may allocate.
    void prepareBuffers (double sessionSampleRate, int maxBlockSize)
    {
        const double rate = modelSampleRate > 0.0 ? modelSampleRate : sessionSampleRate;
        needsResampling = std::abs (rate - sessionSampleRate) > 1.0e-6;

        const double ratio = needsResampling ? (rate / sessionSampleRate) : 1.0;
        maxModelFrames = needsResampling
            ? (int) std::ceil ((double) maxBlockSize * ratio) + 8
            : maxBlockSize;
        maxModelFrames = juce::jmax (1, maxModelFrames);

        modelRateInputScratch.assign ((size_t) maxModelFrames, 0.0f);
        modelRateOutputScratch.assign ((size_t) maxModelFrames, 0.0f);
        namInputScratch.assign ((size_t) maxModelFrames, 0.0);
        namOutputScratch.assign ((size_t) maxModelFrames, 0.0);

        inputResampler.reset();
        outputResampler.reset();

        // dsp->Reset() prewarms by default -- fine here, we're on the loader
        // thread, never the audio thread.
        dsp->Reset (rate, maxModelFrames);
    }
};

//==============================================================================
struct NamProcessor::Impl
{
    static constexpr double kCrossfadeSeconds = 0.010;
    // Comfortably longer than one crossfade and than any plausible in-flight
    // audio callback, so it's safe to free a retired model after this delay
    // -- see requestLoad().
    static constexpr double kRetireGraceSeconds = 0.25;

    // Set from prepare(); read by the loader thread when sizing a new model.
    std::atomic<double> sessionSampleRate { 44100.0 };
    std::atomic<int> maxBlockSize { 512 };

    // Published by the loader thread (exchange), read by the audio thread
    // (load) and by the message-thread query methods (load). Never mutated
    // in place.
    std::atomic<LoadedModel*> activeModel { nullptr };
    std::atomic<bool> normalizeEnabled { false };

    // Background loads in flight; the destructor waits for this to hit zero
    // before tearing down (so no loader thread is left touching `this`).
    std::atomic<int> inFlightLoads { 0 };

    // --- Audio-thread-only state below: never touched by any other thread ---
    LoadedModel* audioThreadCurrentModel = nullptr; // last value observed from activeModel
    LoadedModel* fadeFromModel = nullptr;           // nullptr means "fading from silence"
    int crossfadeLength = 1;
    int crossfadeRemaining = 0;

    std::vector<float> monoScratch;
    std::vector<float> fadeFromScratch;
    std::vector<float> fadeToScratch;

    ~Impl()
    {
        delete activeModel.load (std::memory_order_acquire);
    }

    // Runs `model` on `numSamples` of mono input, writing session-rate mono
    // output. `sessionOut` may alias `monoIn`. Audio-thread only.
    void runModel (LoadedModel& model, const float* monoIn, float* sessionOut, int numSamples) noexcept
    {
        if (! model.needsResampling)
        {
            for (int i = 0; i < numSamples; ++i)
                model.namInputScratch[(size_t) i] = (double) monoIn[i];

            double* namIn[]  = { model.namInputScratch.data() };
            double* namOut[] = { model.namOutputScratch.data() };
            model.dsp->process (namIn, namOut, numSamples);

            for (int i = 0; i < numSamples; ++i)
                sessionOut[i] = (float) model.namOutputScratch[(size_t) i];
        }
        else
        {
            const double sessionRate = sessionSampleRate.load (std::memory_order_relaxed);
            const double modelRate   = model.modelSampleRate;

            const int numModelFrames = juce::jlimit (1, model.maxModelFrames,
                (int) std::ceil ((double) numSamples * modelRate / sessionRate));

            model.inputResampler.process (sessionRate / modelRate,
                                           monoIn,
                                           model.modelRateInputScratch.data(),
                                           numModelFrames,
                                           numSamples,
                                           0);

            for (int i = 0; i < numModelFrames; ++i)
                model.namInputScratch[(size_t) i] = (double) model.modelRateInputScratch[(size_t) i];

            double* namIn[]  = { model.namInputScratch.data() };
            double* namOut[] = { model.namOutputScratch.data() };
            model.dsp->process (namIn, namOut, numModelFrames);

            for (int i = 0; i < numModelFrames; ++i)
                model.modelRateOutputScratch[(size_t) i] = (float) model.namOutputScratch[(size_t) i];

            model.outputResampler.process (modelRate / sessionRate,
                                            model.modelRateOutputScratch.data(),
                                            sessionOut,
                                            numSamples,
                                            numModelFrames,
                                            0);
        }

        if (normalizeEnabled.load (std::memory_order_relaxed) && model.hasLoudness)
        {
            const float g = model.normalizationGain;
            for (int i = 0; i < numSamples; ++i)
                sessionOut[i] *= g;
        }
    }
};

//==============================================================================
NamProcessor::NamProcessor() : impl (std::make_unique<Impl>())
{
}

NamProcessor::~NamProcessor()
{
    // Wait (briefly) for any in-flight background loads to finish and
    // deliver their callback before `impl` is torn down below -- a detached
    // loader thread must never touch `impl` after this point. This runs on
    // whichever thread owns the NamProcessor; never the audio thread.
    while (impl->inFlightLoads.load (std::memory_order_acquire) > 0)
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
}

void NamProcessor::prepare (double sampleRate, int maxBlockSize) noexcept
{
    impl->sessionSampleRate.store (sampleRate, std::memory_order_relaxed);
    impl->maxBlockSize.store (maxBlockSize, std::memory_order_relaxed);

    impl->crossfadeLength = juce::jmax (1, (int) std::round (sampleRate * Impl::kCrossfadeSeconds));
    impl->crossfadeRemaining = 0;
    impl->fadeFromModel = nullptr;
    impl->audioThreadCurrentModel = impl->activeModel.load (std::memory_order_acquire);

    impl->monoScratch.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
    impl->fadeFromScratch.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
    impl->fadeToScratch.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
}

void NamProcessor::reset() noexcept
{
    impl->crossfadeRemaining = 0;
    impl->fadeFromModel = nullptr;

    if (auto* current = impl->audioThreadCurrentModel)
    {
        current->inputResampler.reset();
        current->outputResampler.reset();
    }
}

void NamProcessor::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    LoadedModel* latest = impl->activeModel.load (std::memory_order_acquire);

    if (latest != impl->audioThreadCurrentModel)
    {
        impl->fadeFromModel = impl->audioThreadCurrentModel;
        impl->audioThreadCurrentModel = latest;
        impl->crossfadeRemaining = impl->crossfadeLength;
    }

    // Nothing loaded and no in-flight fade: transparent passthrough.
    if (impl->audioThreadCurrentModel == nullptr && impl->crossfadeRemaining == 0)
        return;

    jassert ((int) impl->monoScratch.size() >= numSamples);
    float* mono = impl->monoScratch.data();

    {
        const float scale = numChannels > 1 ? (1.0f / (float) numChannels) : 1.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                sum += buffer.getReadPointer (ch)[i];
            mono[i] = sum * scale;
        }
    }

    if (impl->crossfadeRemaining > 0)
    {
        float* fadeToBuf = impl->fadeToScratch.data();
        if (impl->audioThreadCurrentModel != nullptr)
            impl->runModel (*impl->audioThreadCurrentModel, mono, fadeToBuf, numSamples);
        else
            juce::FloatVectorOperations::clear (fadeToBuf, numSamples);

        float* fadeFromBuf = impl->fadeFromScratch.data();
        if (impl->fadeFromModel != nullptr)
            impl->runModel (*impl->fadeFromModel, mono, fadeFromBuf, numSamples);
        else
            juce::FloatVectorOperations::clear (fadeFromBuf, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            if (impl->crossfadeRemaining > 0)
            {
                const double t = 1.0 - (double) impl->crossfadeRemaining / (double) impl->crossfadeLength;
                const float gFrom = (float) std::cos (t * juce::MathConstants<double>::halfPi);
                const float gTo   = (float) std::sin (t * juce::MathConstants<double>::halfPi);
                mono[i] = fadeFromBuf[i] * gFrom + fadeToBuf[i] * gTo;
                --impl->crossfadeRemaining;
            }
            else
            {
                mono[i] = fadeToBuf[i];
            }
        }
    }
    else
    {
        impl->runModel (*impl->audioThreadCurrentModel, mono, mono, numSamples);
    }

    for (int ch = 0; ch < numChannels; ++ch)
        buffer.copyFrom (ch, 0, mono, numSamples);
}

void NamProcessor::requestLoad (const juce::String& namFilePath,
                                 std::function<void (bool ok, juce::String errorOrName)> onDone)
{
    Impl* implPtr = impl.get();
    const double sessionRate = implPtr->sessionSampleRate.load (std::memory_order_relaxed);
    const int blockSize      = implPtr->maxBlockSize.load (std::memory_order_relaxed);
    const juce::String pathCopy = namFilePath;

    implPtr->inFlightLoads.fetch_add (1, std::memory_order_acq_rel);

    std::thread ([implPtr, pathCopy, sessionRate, blockSize, onDone = std::move (onDone)]() mutable
    {
        bool ok = false;
        juce::String result;
        std::unique_ptr<LoadedModel> newModel;

        try
        {
            auto model = std::make_unique<LoadedModel>();
            nam::dspData config;

            // Build the filesystem path from UTF-16 so non-ASCII paths round
            // trip correctly on Windows (a plain std::string ctor would go
            // through the narrow/ANSI code page instead).
            std::filesystem::path fsPath (pathCopy.toWideCharPointer());

            model->dsp = nam::get_dsp (fsPath, config);
            if (model->dsp == nullptr)
                throw std::runtime_error ("No DSP could be constructed for this .nam file");

            model->modelSampleRate = model->dsp->GetExpectedSampleRate();

            juce::String nameFromMetadata;
            if (config.metadata.is_object())
            {
                auto it = config.metadata.find ("name");
                if (it != config.metadata.end() && it->is_string())
                    nameFromMetadata = juce::String (it->get<std::string>());
            }
            model->name = nameFromMetadata.isNotEmpty()
                ? nameFromMetadata
                : juce::File (pathCopy).getFileNameWithoutExtension();

            model->hasLoudness = model->dsp->HasLoudness();
            model->normalizationGain = model->hasLoudness
                ? juce::Decibels::decibelsToGain ((float) (kTargetLoudnessDb - model->dsp->GetLoudness()))
                : 1.0f;

            model->prepareBuffers (sessionRate, blockSize);

            newModel = std::move (model);
            ok = true;
            result = newModel->name;
        }
        catch (const std::exception& e)
        {
            ok = false;
            result = juce::String (e.what());
        }
        catch (...)
        {
            ok = false;
            result = "Unknown error loading .nam file";
        }

        if (ok && newModel != nullptr)
        {
            LoadedModel* published = newModel.release();
            LoadedModel* old = implPtr->activeModel.exchange (published, std::memory_order_acq_rel);

            if (old != nullptr)
            {
                // Give the audio thread comfortably more than one crossfade's
                // worth of time to finish reading `old` before we free it.
                // Never delete on the audio thread; this is the loader thread.
                std::this_thread::sleep_for (
                    std::chrono::duration<double> (Impl::kRetireGraceSeconds));
                delete old;
            }
        }

        if (onDone)
        {
            juce::MessageManager::callAsync ([onDone, ok, result]
            {
                onDone (ok, result);
            });
        }

        implPtr->inFlightLoads.fetch_sub (1, std::memory_order_acq_rel);
    }).detach();
}

bool NamProcessor::isLoaded() const noexcept
{
    return impl->activeModel.load (std::memory_order_acquire) != nullptr;
}

juce::String NamProcessor::getModelName() const noexcept
{
    if (auto* m = impl->activeModel.load (std::memory_order_acquire))
        return m->name;
    return {};
}

void NamProcessor::setNormalize (bool shouldNormalize) noexcept
{
    impl->normalizeEnabled.store (shouldNormalize, std::memory_order_relaxed);
}

double NamProcessor::getModelSampleRate() const noexcept
{
    if (auto* m = impl->activeModel.load (std::memory_order_acquire))
        return m->modelSampleRate;
    return 0.0;
}

} // namespace sg
