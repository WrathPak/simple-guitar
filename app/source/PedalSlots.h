#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>

/**
    The six generic pedal slots that replaced the fixed screamer/echoes/
    chamber trio (see PluginProcessor.h's class-level comment for the full
    chain picture). This header has two independent halves:

      - Pure, JUCE-light helpers (PedalType, computeMovePermutation()) --
        directly Catch2-testable without a running audio engine, same spirit
        as the old (now-removed) ChainOrder.h was.
      - PedalSlotHost, the runtime piece: one live PedalInstance per slot,
        built+prepared on the message thread and atomically swapped into the
        audio thread's view with a short crossfade whenever a slot's pedal
        type changes -- mirrors sg::NamProcessor's model-swap pattern
        (engine/include/sg/NamProcessor.h) applied to a whole effect chain
        slot instead of a single model pointer.

    Slot order is signal order: slot 0 runs first (right after the noise
    gate), slot 5 feeds the NAM amp. There is no separate "chain order"
    concept anymore -- reordering (movePedal) permutes the SLOTS' PARAMETER
    VALUES (see PluginProcessor::movePedal()), not which DSP instance sits
    where; the slot index itself is always the signal position.
*/
namespace sg
{

/** The eight possible occupants of a pedal slot. Values match
    schema/bridge.schema.json's PedalTypeId exactly (also the raw/denormalized
    value AudioProcessorValueTreeState stores for each slot{N}Type
    AudioParameterChoice) -- never renumber these without bumping the bridge
    schema version and the migration logic in StateMigration.h.

    `plugin` (7) is a hosted third-party plugin slot: which plugin occupies
    it lives OUTSIDE the param system, in the slot{N}PluginId/PluginState
    state-tree properties (see PluginProcessor.h), because a plugin identity
    isn't a host-automatable value. createPedalInstance (plugin, ...) only
    ever builds the passthrough placeholder -- the real hosted instance is
    built asynchronously on the message thread and injected via
    PedalSlotHost::swapSlotInstance() (see PluginHosting.h). */
enum class PedalType : int
{
    empty = 0,
    screamer = 1,
    echoes = 2,
    chamber = 3,
    squeeze = 4,
    wobble = 5,
    shiver = 6,
    plugin = 7
};

constexpr int numPedalTypes = 8;
constexpr int numSlots = 6;
constexpr int numParamsPerSlot = 4;

constexpr bool isValidPedalType (int v) noexcept { return v >= 0 && v < numPedalTypes; }
constexpr bool isValidSlotIndex (int slot) noexcept { return slot >= 0 && slot < numSlots; }

/** Builds a slot's APVTS param id, e.g. slotParamId(2, "Type") == "slot2Type".
    `suffix` is one of "Type", "On", "P1", "P2", "P3", "P4". Matches
    PluginProcessor's slot{N}{suffix} param ids exactly -- shared by
    StateMigration.cpp and PluginProcessor.cpp so the naming scheme only
    exists in one place. */
inline juce::String slotParamId (int slot, const juce::String& suffix)
{
    return "slot" + juce::String (slot) + suffix;
}

using SlotPermutation = std::array<int, numSlots>;

/** Pure helper backing PluginProcessor::movePedal(): given a
    movePedal(from, to) gesture, returns, for every NEW slot index i, which
    OLD slot index's param values (type/on/P1-4) should be copied there --
    a standard "remove at from, insert at to" array move over all 6 slots
    (sparse/empty slots are moved just like any other slot's values), with
    everything strictly between from and to shifting by one to close the
    gap: left if from < to, right if from > to. Returns the identity
    permutation for out-of-range indices or from == to. Pure, no JUCE
    dependency -- directly Catch2-testable. */
inline SlotPermutation computeMovePermutation (int from, int to) noexcept
{
    SlotPermutation result {};
    for (int i = 0; i < numSlots; ++i)
        result[(std::size_t) i] = i;

    if (! isValidSlotIndex (from) || ! isValidSlotIndex (to) || from == to)
        return result;

    int without[numSlots - 1];
    int n = 0;
    for (int i = 0; i < numSlots; ++i)
        if (i != from)
            without[n++] = i;

    int out[numSlots];
    int ti = 0;
    for (int i = 0; i < numSlots; ++i)
        out[i] = (i == to) ? from : without[ti++];

    for (int i = 0; i < numSlots; ++i)
        result[(std::size_t) i] = out[i];

    return result;
}

//==============================================================================
// Runtime slot hosting.

/** Generic run-time interface every concrete pedal-type adapter implements
    (see PedalSlots.cpp), so PedalSlotHost can hold and drive any of the six
    real pedal types -- or "empty" -- uniformly. process()/setEnabled()/
    setParam() are audio-thread contracts: no locks, no allocation, mirroring
    every concrete engine pedal class's own contract (Screamer, StereoDelay,
    ...). An instance is only ever handed to a slot fully constructed and
    prepared (see createPedalInstance()) -- nothing here builds or allocates
    on the audio thread. */
class PedalInstance
{
public:
    virtual ~PedalInstance() = default;

    /** Audio-thread only: no locks, no allocation. */
    virtual void process (juce::AudioBuffer<float>& buffer) noexcept = 0;

    /** Audio-thread safe (atomic store only). */
    virtual void setEnabled (bool shouldBeEnabled) noexcept = 0;

    /** Sets one of the slot's four generic knobs, normalized [0, 1].
        `index` is 0..3 for P1..P4; a type that doesn't use every knob
        (e.g. screamer ignores P4) silently ignores the unused index.
        Audio-thread safe (atomic store only). */
    virtual void setParam (int index, float v01) noexcept = 0;

    /** Fixed processing latency in samples, if any -- only screamer-type
        instances ever report nonzero (their internal 2x oversampling; see
        engine/include/sg/Screamer.h). Every other type (including empty)
        defaults to 0. */
    virtual int getLatencySamples() const noexcept { return 0; }
};

/** Builds and fully prepares (prepare() already called against the given
    sample rate/block size/channel count) a new PedalInstance for `type`.
    Message-thread only -- may allocate. Never returns nullptr; an "empty"
    instance is a valid, cheap no-op passthrough, not a null pointer, so
    every slot always has something live to call process() on. */
std::unique_ptr<PedalInstance> createPedalInstance (PedalType type, double sampleRate, int maxBlockSize, int numChannels);

/**
    Owns the six pedal slots' live PedalInstance pointers and runs the
    swap-with-crossfade dance for a type change, mirroring
    sg::NamProcessor's model-swap pattern: a new instance is built+prepared
    entirely on the message thread (createPedalInstance(), called from
    swapSlot()), then published via an atomic exchange; the audio thread
    picks up the swap on its next block (processSlot()) and crossfades
    between the OLD and NEW instance's output over a short, fixed ~10ms
    window so the swap is click-free -- both instances run the same input
    audio in parallel (via per-slot scratch buffers sized in prepare()) for
    the duration of the fade. The old instance is never destroyed on the
    audio thread: swapSlot() hands the retired pointer back to the caller,
    which owns it from that point on and MUST hold it until
    canRetireSafely() says the audio thread provably can no longer observe
    it before actually deleting it. PluginProcessor does this via a
    message-thread-only deferred garbage collection list, the same "never
    delete on the audio thread" rule NamProcessor's loader thread itself
    follows (see engine/src/NamProcessor.cpp's kRetireGraceSeconds).

    Retire-safety proof (canRetireSafely()): a wall-clock grace period alone
    is NOT sufficient -- the crossfade consumes AUDIO time, so if the host
    stops calling processBlock right after a swap and resumes later, the
    audio thread can pick the swap up (and read the retired instance for a
    whole crossfade) arbitrarily long after any fixed wall-clock delay has
    elapsed. Instead the audio thread PUBLISHES (release-stores) the only
    two pointers it can still touch for a slot -- its current instance and
    its in-flight crossfade source -- at the exact transition points where
    they change (swap pickup, fade completion). A retired pointer is
    provably unobservable, and therefore safe to delete, exactly when it is
    none of: the slot's `active` pointer (the audio thread could still pick
    it up), the published current, or the published fade source -- because
    the audio thread only ever acquires instances from `active`, and
    `fadeFrom` is only ever assigned from its own previous current, so a
    pointer absent from all three can never re-enter the audio thread's
    view. The release/acquire pairing makes every audio-thread access to
    the retired instance happen-before a canRetireSafely()==true
    observation on the message thread. If the audio thread never runs
    again, retired instances are simply held (conservatively) until the
    next prepare() or destruction -- never freed while potentially
    observable.

    Threading model:
      - prepare()/swapSlot() are message-thread only (build/prepare/exchange;
        may allocate).
      - processSlot()/getReportedLatencySamples() (from the audio thread's
        perspective) are audio-thread only: read one atomic pointer per slot
        per block, no lock, no allocation.
*/
class PedalSlotHost
{
public:
    PedalSlotHost();
    ~PedalSlotHost();

    /** (Re)builds every slot's instance directly, with no crossfade --
        prepare() is only ever called when processing is guaranteed not to
        be running concurrently (same assumption every other engine block's
        own prepare() makes throughout this codebase). `types` gives each
        slot's starting PedalType. Message-thread only; may allocate. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels, const std::array<PedalType, numSlots>& types);

    /** Builds a new instance for `type` and atomically swaps it into `slot`,
        to be picked up by the audio thread on its next block with a short
        crossfade. Returns the just-retired instance (never null once
        prepare() has run at least once) -- the caller owns it now (see
        class-level comment on the retire/garbage-collection contract).
        Message-thread only; may allocate. */
    std::unique_ptr<PedalInstance> swapSlot (int slot, PedalType type, double sampleRate, int maxBlockSize, int numChannels);

    /** Atomically swaps a caller-built, fully-prepared instance into `slot`
        -- the injection point for instances createPedalInstance() can't
        build itself (a hosted third-party plugin, built asynchronously on
        the message thread; see PluginHosting.h). Identical crossfade/retire
        contract to swapSlot(): returns the just-retired instance, which the
        caller owns and must hold through the grace period. `newInstance`
        must be non-null (hand in an empty/passthrough instance rather than
        null, same invariant as everywhere else). Message-thread only. */
    std::unique_ptr<PedalInstance> swapSlotInstance (int slot, std::unique_ptr<PedalInstance> newInstance);

    /** Processes one slot's audio in place, driven by the given current
        on/off + four generic knob values. Audio-thread only: no locks, no
        allocation. Transparently handles an in-flight crossfade if a
        swapSlot() landed since the last block. */
    void processSlot (int slotIndex, bool on, const std::array<float, numParamsPerSlot>& params, juce::AudioBuffer<float>& buffer) noexcept;

    /** The fixed processing latency in samples currently reported by the
        live (crossfade-target) instance in `slot`, or 0 if `on` is false --
        matches the old single-screamer contract (see PluginProcessor's
        class-level comment): a bypassed pedal contributes no latency since
        it's a bit-exact passthrough, not an actually-delayed signal. Safe to
        call from any thread (atomic load) -- intended for PluginProcessor's
        message-thread updateReportedLatency() poll. */
    int getReportedLatencySamples (int slotIndex, bool on) const noexcept;

    /** True once the audio thread provably can no longer observe `retired`
        in `slot` (see the class-level retire-safety proof): it is neither
        the slot's published-active instance nor either of the two pointers
        the audio thread has release-published as still in use (its current
        instance and its in-flight crossfade source). Callers holding a
        retired instance poll this from the message thread and only delete
        once it returns true (PluginProcessor::collectRetiredPedals()).
        Message-thread use (any non-audio thread is safe -- acquire loads
        only). Returns true for out-of-range slots (nothing can observe
        anything there). */
    bool canRetireSafely (int slotIndex, const PedalInstance* retired) const noexcept;

    /** ~10ms, matches sg::NamProcessor's own crossfade window. */
    static constexpr double crossfadeSeconds = 0.010;

private:
    struct Slot
    {
        std::atomic<PedalInstance*> active { nullptr };

        // Audio-thread-only state below (audioThreadCurrent/fadeFrom/
        // crossfade counters/scratch): never touched by any other thread.
        PedalInstance* audioThreadCurrent = nullptr;
        PedalInstance* fadeFrom = nullptr;
        int crossfadeLength = 1;
        int crossfadeRemaining = 0;

        // Release-published mirrors of audioThreadCurrent/fadeFrom, written
        // by the audio thread at the swap-pickup and fade-completion
        // transition points and acquire-read by canRetireSafely() on the
        // message thread (see the class-level retire-safety proof).
        // prepare() (re)seeds them directly -- processing is guaranteed
        // stopped there.
        std::atomic<PedalInstance*> publishedCurrent { nullptr };
        std::atomic<PedalInstance*> publishedFadeFrom { nullptr };

        juce::AudioBuffer<float> fadeFromScratch, fadeToScratch;
    };

    std::array<Slot, numSlots> slots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalSlotHost)
};

} // namespace sg
