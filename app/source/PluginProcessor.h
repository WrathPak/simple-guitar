#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>

#include <sg/Gain.h>
#include <sg/IrLoader.h>
#include <sg/MeterTap.h>
#include <sg/NamProcessor.h>
#include <sg/NoiseGate.h>
#include <sg/PostEq.h>

#include "PedalSlots.h"
#include "PresetStore.h"
#include "RigLibrary.h"

/**
    M2 rig processor: input trim -> NoiseGate -> [6 generic pedal slots, in
    slot-index order] -> NamProcessor -> PostEq -> IrLoader (cab) -> output
    gain -> MeterTap.

    All engine setters are driven from APVTS atomics once per block (see
    processBlock()) -- the audio thread never touches the APVTS itself,
    only the cached std::atomic<float>* pointers obtained from
    getRawParameterValue() at construction time.

    Pedal slots (see PedalSlots.h for the runtime slot-hosting design): each
    of the 6 slots is a generic {type, on, P1, P2, P3, P4} parameter group
    (slot{N}Type/On/P1../P4, N = 0..5) rather than a fixed pedal. Slot index
    IS signal order -- slot 0 runs right after the gate, slot 5 feeds the
    NAM amp -- so there is no separate "chain order" concept anymore.
    Reordering (movePedal()) permutes the SLOTS' PARAMETER VALUES on the
    message thread via setValueNotifyingHost (type/on/P1-4 move together
    with the pedal), so host automation lanes stay slot-positional; it never
    touches the underlying DSP instances directly; the slot-type-change
    machinery below picks up the resulting value changes and rebuilds
    whichever slots actually changed type.

    A slot's TYPE change never touches the audio thread: a new pedal
    instance is built+prepared entirely on the message thread
    (PedalSlotHost::swapSlot(), see PedalSlots.h), then atomically published;
    the audio thread crossfades between the old and new instance over a
    short (~10ms) window on its next few blocks (PedalSlotHost::processSlot()).
    The just-retired instance is never deleted on the audio thread -- it's
    held in retiringPedals (message-thread only) until a short grace period
    has passed (collectRetiredPedals(), run from the same low-rate timer as
    updateReportedLatency() below), mirroring sg::NamProcessor's own
    loader-thread retire-then-delete pattern.

    Slot-type changes can originate three ways, all funnelled through the
    single applySlotTypeIfChanged() helper so the "detect a type change,
    rebuild the instance" logic only exists once:
      - setSlotType() (WebviewBridge's "setSlotType" message): a genuine
        user pick of a new pedal for a slot -- additionally resets that
        slot's On/P1-4 to their flat defaults (true/0.5), since a newly
        chosen pedal's knobs starting at the previous occupant's values
        rarely means anything. Applied synchronously (already message
        thread, called directly from the bridge's event listener).
      - movePedal() (WebviewBridge's "movePedal" message): does NOT reset
        anything -- values travel with the pedal. Applied synchronously.
      - The message-thread poll (timerCallback(), see updateReportedLatency's
        own class-level reasoning for why this can't be a parameter
        listener): backstops host automation writing directly to a
        slot{N}Type parameter without going through either bridge message.
        Deliberately does NOT reset On/P1-4 -- an automated type change is a
        raw parameter write like any other, not a "pick a new pedal from the
        palette" user gesture.

    NAM model / IR loading and the managed library scan happen on the
    message thread; see RigLibrary.h and WebviewBridge for the load/rigState
    flow. The currently-loaded model/IR paths are persisted as properties on
    the APVTS state tree (see getStateInformation/setStateInformation) and
    restored on state load; the 36 slot params are ordinary APVTS parameters
    and round-trip through the state tree on their own.

    Legacy state migration (see StateMigration.h): setStateInformation() and
    loadPreset() both run every incoming state XML through
    sg::migrateStateXmlIfNeeded() before apvts.replaceState() -- a state
    saved before the slot rework (fixed screamer/echoes/chamber params plus
    a chainOrder attribute) is transparently rewritten into the current
    slot-param shape (chainOrder position i -> slot i; anything beyond the
    old chain's 3 pedals comes out empty) before APVTS ever sees it. A
    current-format state XML passes through unchanged.

    Screamer-type slots are the only ones that report nonzero latency
    (PedalInstance::getLatencySamples(), only meaningful once prepare() has
    run). Total plugin latency (setLatencySamples()) must only ever be
    touched from the message thread (JUCE's updateHostDisplay() plumbing
    behind it is not audio-thread-safe), so it's recomputed from
    prepareToPlay() and from a low-rate message-thread timer here
    (independent of whether an editor/WebviewBridge exists, so latency
    compensation still works for a headless host or pluginval run with no
    editor open) rather than from a parameter listener callback, which JUCE
    may invoke from the audio thread for host automation.

    Presets (see PresetStore.h and the preset-manager section below): a flat
    library of *.sgpreset files under Documents/Simple Guitar/Presets,
    save/load/next/prev all message-thread only. loadPreset() reuses
    setStateInformation's own restore path (apvts.replaceState() +
    restoreLoadedPathsFromState()) so model/IR/slot params restore
    identically whether the state came from a DAW session or a preset file.
    Dirty tracking can't rely on AudioProcessorValueTreeState parameter
    listeners for the same reason updateReportedLatency() above can't --
    JUCE may invoke them from the audio thread for host automation -- so
    "did any param change since the last save/load" is answered the same way
    latency is: a low-rate message-thread poll (see timerCallback()) that
    diffs every param's current normalized value against a baseline snapshot
    taken at the last save/load. Model/IR-load and slot-type/reorder
    dirtying are set directly at their own call sites instead (not on the
    poll), since a type/reorder change to a *choice* param still shows up on
    the generic per-param poll too, but the explicit set here makes it
    immediate rather than waiting for the next poll tick.
*/
class SimpleGuitarAudioProcessor final : public juce::AudioProcessor,
                                          private juce::Timer
{
public:
    SimpleGuitarAudioProcessor();
    ~SimpleGuitarAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Parameter ids -- match schema/bridge.schema.json's ParamId enum exactly.
    static constexpr const char* outputGainParamId = "outputGain";
    static constexpr const char* gateOnParamId = "gateOn";
    static constexpr const char* gateThresholdDbParamId = "gateThresholdDb";
    static constexpr const char* ampInputDbParamId = "ampInputDb";
    static constexpr const char* ampBassDbParamId = "ampBassDb";
    static constexpr const char* ampMidDbParamId = "ampMidDb";
    static constexpr const char* ampTrebleDbParamId = "ampTrebleDb";
    static constexpr const char* ampPresenceDbParamId = "ampPresenceDb";
    static constexpr const char* namNormalizeParamId = "namNormalize";
    static constexpr const char* cabOnParamId = "cabOn";
    static constexpr const char* cabLowCutHzParamId = "cabLowCutHz";
    static constexpr const char* cabHighCutHzParamId = "cabHighCutHz";

    // Pedal slots: slot{N}Type/On/P1/P2/P3/P4 for N = 0..5 (36 ids total).
    // slotTypeParamId(n)/slotOnParamId(n)/slotPParamId(n, p) below build
    // these same ids at runtime (matches sg::slotParamId in PedalSlots.h);
    // the named constants exist for the two callers (allParamIds and
    // WebviewBridge) that want plain compile-time string literals.
    static constexpr const char* slot0TypeParamId = "slot0Type";
    static constexpr const char* slot0OnParamId = "slot0On";
    static constexpr const char* slot0P1ParamId = "slot0P1";
    static constexpr const char* slot0P2ParamId = "slot0P2";
    static constexpr const char* slot0P3ParamId = "slot0P3";
    static constexpr const char* slot0P4ParamId = "slot0P4";
    static constexpr const char* slot1TypeParamId = "slot1Type";
    static constexpr const char* slot1OnParamId = "slot1On";
    static constexpr const char* slot1P1ParamId = "slot1P1";
    static constexpr const char* slot1P2ParamId = "slot1P2";
    static constexpr const char* slot1P3ParamId = "slot1P3";
    static constexpr const char* slot1P4ParamId = "slot1P4";
    static constexpr const char* slot2TypeParamId = "slot2Type";
    static constexpr const char* slot2OnParamId = "slot2On";
    static constexpr const char* slot2P1ParamId = "slot2P1";
    static constexpr const char* slot2P2ParamId = "slot2P2";
    static constexpr const char* slot2P3ParamId = "slot2P3";
    static constexpr const char* slot2P4ParamId = "slot2P4";
    static constexpr const char* slot3TypeParamId = "slot3Type";
    static constexpr const char* slot3OnParamId = "slot3On";
    static constexpr const char* slot3P1ParamId = "slot3P1";
    static constexpr const char* slot3P2ParamId = "slot3P2";
    static constexpr const char* slot3P3ParamId = "slot3P3";
    static constexpr const char* slot3P4ParamId = "slot3P4";
    static constexpr const char* slot4TypeParamId = "slot4Type";
    static constexpr const char* slot4OnParamId = "slot4On";
    static constexpr const char* slot4P1ParamId = "slot4P1";
    static constexpr const char* slot4P2ParamId = "slot4P2";
    static constexpr const char* slot4P3ParamId = "slot4P3";
    static constexpr const char* slot4P4ParamId = "slot4P4";
    static constexpr const char* slot5TypeParamId = "slot5Type";
    static constexpr const char* slot5OnParamId = "slot5On";
    static constexpr const char* slot5P1ParamId = "slot5P1";
    static constexpr const char* slot5P2ParamId = "slot5P2";
    static constexpr const char* slot5P3ParamId = "slot5P3";
    static constexpr const char* slot5P4ParamId = "slot5P4";

    /** Every param id above, in a fixed, stable order shared with WebviewBridge. */
    static constexpr int numParams = 12 + sg::numSlots * (2 + sg::numParamsPerSlot);
    static constexpr std::array<const char*, numParams> allParamIds { {
        outputGainParamId,
        gateOnParamId,
        gateThresholdDbParamId,
        ampInputDbParamId,
        ampBassDbParamId,
        ampMidDbParamId,
        ampTrebleDbParamId,
        ampPresenceDbParamId,
        namNormalizeParamId,
        cabOnParamId,
        cabLowCutHzParamId,
        cabHighCutHzParamId,
        slot0TypeParamId, slot0OnParamId, slot0P1ParamId, slot0P2ParamId, slot0P3ParamId, slot0P4ParamId,
        slot1TypeParamId, slot1OnParamId, slot1P1ParamId, slot1P2ParamId, slot1P3ParamId, slot1P4ParamId,
        slot2TypeParamId, slot2OnParamId, slot2P1ParamId, slot2P2ParamId, slot2P3ParamId, slot2P4ParamId,
        slot3TypeParamId, slot3OnParamId, slot3P1ParamId, slot3P2ParamId, slot3P3ParamId, slot3P4ParamId,
        slot4TypeParamId, slot4OnParamId, slot4P1ParamId, slot4P2ParamId, slot4P3ParamId, slot4P4ParamId,
        slot5TypeParamId, slot5OnParamId, slot5P1ParamId, slot5P2ParamId, slot5P3ParamId, slot5P4ParamId,
    } };

    /** Read-only access to the post-chain meter tap, e.g. for a UI-side timer
        or the webview bridge to push meterFrame events from. */
    sg::MeterTap& getMeterTap() noexcept { return meterTap; }

    /** Engine block access for WebviewBridge (rig state / load requests).
        Message-thread use only -- the audio thread only touches these
        through processBlock()'s own cached-atomic-driven calls. */
    sg::NamProcessor& getNam() noexcept { return nam; }
    sg::IrLoader& getCab() noexcept { return cab; }

    /** Rescans the managed library folders. Message-thread only. */
    sg::RigLibrary scanRigLibrary() const { return sg::scanLibrary(); }

    /** Requests a NAM model load and remembers the path (for state persistence)
        on success. Safe to call only from the message thread. onDone is called
        back on the message thread (per sg::NamProcessor's contract). */
    void requestLoadNamModel (const juce::String& path, std::function<void (bool ok, juce::String errorOrName)> onDone);

    /** Requests an IR load and remembers the path (for state persistence) on
        success. Safe to call only from the message thread; onDone is called
        back synchronously (per sg::IrLoader's contract). */
    void requestLoadIr (const juce::String& path, std::function<void (bool ok, juce::String errorOrName)> onDone);

    //==========================================================================
    // Pedal slots. See PedalSlots.h for the runtime slot-hosting design and
    // the class-level comment above for the full setSlotType/movePedal
    // threading story.

    /** Current raw type (0..6) of `slot`, read straight from the APVTS
        param. Safe from any thread (atomic load); message-thread callers
        (WebviewBridge's rigState) are the intended use. */
    int getSlotType (int slot) const noexcept;

    /** Current on/off of `slot`, read straight from the APVTS param. Safe
        from any thread (atomic load). */
    bool getSlotOn (int slot) const noexcept;

    /** Sets `slot`'s pedal type to `pedalType` (0..6) and resets that slot's
        On/P1-4 to their flat defaults (true/0.5) -- a genuine "pick a new
        pedal for this slot" user gesture (see class-level comment). Rebuilds
        the slot's DSP instance immediately (message thread; may allocate).
        Returns false (no-op) if `slot`/`pedalType` are out of range. */
    bool setSlotType (int slot, int pedalType);

    /** Moves the pedal at slot `from` to slot `to`: every slot strictly
        between them shifts by one to close the gap (see
        sg::computeMovePermutation()), and each affected slot's param VALUES
        (type/on/P1-4) move together via setValueNotifyingHost -- host
        automation lanes stay slot-positional, nothing about the underlying
        DSP instances is touched directly (the resulting type-value changes
        are picked up and rebuilt the same way any other type change is).
        Message-thread only (may allocate, for any slot whose type actually
        changed). Returns false (no-op) if `from`/`to` are out of range;
        from == to is a harmless no-op that still returns true. */
    bool movePedal (int from, int to);

    //==========================================================================
    // Presets. See PresetStore.h for the on-disk format and the class-level
    // comment above for the dirty-tracking design. Message-thread only --
    // same rule as the NAM/IR loaders and setSlotType/movePedal above.

    struct CurrentPreset
    {
        juce::String name;
        juce::String path;
    };

    /** Rescans the managed Presets folder, sorted by name. */
    std::vector<sg::PresetEntry> listPresets() const { return sg::scanPresets(); }

    /** The current preset (name+path), if any preset has been saved to or
        loaded this session (or restored from a DAW session's persisted
        currentPresetPath/currentPresetName properties). */
    std::optional<CurrentPreset> getCurrentPreset() const;

    /** True if any APVTS param, or the loaded model/IR, has changed since
        the last save/load (see the class-level comment for how this is
        tracked). Safe to call from any thread (atomic load), though only
        ever meaningfully written from the message thread. */
    bool isPresetDirty() const noexcept { return presetDirty.load (std::memory_order_relaxed); }

    /** Overwrites the current preset with the current rig state. False (no-op)
        if there is no current preset -- callers should fall back to
        savePresetAs() in that case. */
    bool saveCurrentPreset (juce::String& outError);

    /** Saves (or overwrites) a preset named `rawName` and makes it current.
        `rawName` is sanitized into a safe filename; overwrite-if-a-preset-
        with-that-name-already-exists is intentional, not an error. Fails
        only if nothing sanitizable remains of the name, or the write itself
        fails. */
    bool savePresetAs (const juce::String& rawName, juce::String& outError);

    /** Loads the preset at `path` (must resolve inside the managed Presets
        folder) via the same restore path setStateInformation uses, so
        model/IR/slot params restore identically to a DAW session load. */
    bool loadPreset (const juce::String& path, juce::String& outError);

    /** Steps to the next/previous preset (sorted by name) relative to the
        current one, wrapping around; jumps to the first (next) / last (prev)
        preset if none is current yet. False (no-op) if there are no
        presets. */
    bool nextPreset (juce::String& outError);
    bool prevPreset (juce::String& outError);

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Kicks off the state-restore load requests and resyncs the pedal-slot
        instances to the just-restored param values (message thread). Called
        from setStateInformation (marshaled via MessageManager::callAsync
        since the host may call setStateInformation from other threads) and
        directly/synchronously from loadPreset() (already guaranteed
        message-thread, so no marshal needed there) -- the one shared
        restore path both go through. */
    void restoreLoadedPathsFromState();

    /** requestLoadNamModel/requestLoadIr's real implementation, with an
        extra `isRestoring` flag: true from restoreLoadedPathsFromState()
        (DAW session restore or a preset load -- must NOT mark the preset
        dirty, since restoring to a known-good state is the opposite of
        dirtying it), false from the public methods below (a genuine
        user-driven load via the bridge -- must mark it dirty on success). */
    void requestLoadNamModelInternal (const juce::String& path, std::function<void (bool, juce::String)> onDone, bool isRestoring);
    void requestLoadIrInternal (const juce::String& path, std::function<void (bool, juce::String)> onDone, bool isRestoring);

    /** Compares slot `slot`'s current raw type value against
        lastKnownSlotType and, if different, rebuilds its DSP instance
        (PedalSlotHost::swapSlot()) and queues the retired instance for
        deferred deletion (see collectRetiredPedals()). Does NOT touch
        On/P1-4 -- see the class-level comment on why setSlotType() resets
        them itself but this shared helper doesn't. No-op (including doing
        nothing to the slot host) if the slot host hasn't been prepared yet
        -- prepareToPlay()'s own initial build already uses the current
        param values, so there's nothing to rebuild before that. Message
        thread only; may allocate. */
    void applySlotTypeIfChanged (int slot);

    /** Runs applySlotTypeIfChanged() over every slot -- the message-thread
        poll's backstop for host automation writing a slot{N}Type param
        directly (see timerCallback()), and also used right after a state
        restore (restoreLoadedPathsFromState()) to resync every slot in one
        call. Message thread only; may allocate. */
    void syncAllSlotInstancesFromParams();

    /** Deletes any retiringPedals entry whose grace period has elapsed.
        Message-thread only (never the audio thread) -- run from the same
        low-rate timer as updateReportedLatency() below. */
    void collectRetiredPedals();

    /** Recomputes and (if changed) reports total plugin latency to the host
        via setLatencySamples(). Message-thread only -- see the class-level
        comment for why this must never be called from the audio thread. */
    void updateReportedLatency();

    /** Message-thread poll (see the class-level comment on dirty tracking):
        if not already dirty, compares every param's current normalized value
        against dirtyBaselineValues and marks dirty on the first difference
        found. No-op once already dirty -- nothing to do until the next
        save/load refreshes the baseline. */
    void pollForDirtyParamChange();

    /** Snapshots every param's current normalized value into
        dirtyBaselineValues and clears presetDirty. Called once at
        construction and after every successful save/load (including a DAW
        session restore) -- see the class-level comment. */
    void refreshDirtyBaseline();

    /** Shared body of saveCurrentPreset()/savePresetAs(): serializes the
        current apvts state, writes it to `file` as `name`, and updates the
        current-preset bookkeeping (member vars + apvts.state properties)
        and dirty baseline on success. */
    bool writeCurrentStateToPresetFile (const juce::String& name, const juce::File& file, juce::String& outError);

    void timerCallback() override;

    // Property names used on apvts.state to persist the loaded model/IR path
    // and the current preset across getStateInformation/setStateInformation
    // (DAW session save/load).
    static constexpr const char* namModelPathPropertyId = "namModelPath";
    static constexpr const char* irPathPropertyId = "irPath";
    static constexpr const char* currentPresetPathPropertyId = "currentPresetPath";
    static constexpr const char* currentPresetNamePropertyId = "currentPresetName";

    /** How often the message-thread latency-refresh timer runs (see class
        comment): only needs to catch on/off toggles, not track anything
        per-frame, so a low rate is plenty. Also drives the dirty-tracking
        poll (pollForDirtyParamChange()), the slot-type-change backstop poll
        (syncAllSlotInstancesFromParams()), and the retired-pedal garbage
        collection (collectRetiredPedals()) -- same low-rate message-thread
        cadence, same reasoning. */
    static constexpr int latencyRefreshHz = 15;

    //==========================================================================
    // Engine chain: input trim -> gate -> [6 pedal slots] -> nam -> post eq -> cab -> output gain -> meter.
    sg::Gain inputTrimGain;
    sg::NoiseGate gate;
    sg::PedalSlotHost slotHost;
    sg::NamProcessor nam;
    sg::PostEq postEq;
    sg::IrLoader cab;
    sg::Gain outputGainStage;
    sg::MeterTap meterTap;

    // Cached raw pointers into the APVTS parameters' atomic backing values.
    // Read on the audio thread via atomic load only -- processBlock() never
    // dereferences the AudioProcessorValueTreeState itself.
    std::atomic<float>* outputGainDbParam = nullptr;
    std::atomic<float>* gateOnParam = nullptr;
    std::atomic<float>* gateThresholdDbParam = nullptr;
    std::atomic<float>* ampInputDbParam = nullptr;
    std::atomic<float>* ampBassDbParam = nullptr;
    std::atomic<float>* ampMidDbParam = nullptr;
    std::atomic<float>* ampTrebleDbParam = nullptr;
    std::atomic<float>* ampPresenceDbParam = nullptr;
    std::atomic<float>* namNormalizeParam = nullptr;
    std::atomic<float>* cabOnParam = nullptr;
    std::atomic<float>* cabLowCutHzParam = nullptr;
    std::atomic<float>* cabHighCutHzParam = nullptr;

    // Slot params' cached atomics, one array entry per slot. slotTypeParams
    // is read on the message thread only (type never drives the audio
    // thread directly -- only a rebuilt DSP instance does); On/P1-4 are read
    // every block on the audio thread (processBlock()).
    std::array<std::atomic<float>*, sg::numSlots> slotTypeParams {};
    std::array<std::atomic<float>*, sg::numSlots> slotOnParams {};
    std::array<std::array<std::atomic<float>*, sg::numParamsPerSlot>, sg::numSlots> slotPParams {};

    // Message-thread-only cache of each slot's last-seen raw type value, so
    // applySlotTypeIfChanged() only rebuilds a slot whose type actually
    // changed. Seeded to an invalid sentinel (-1) so the very first
    // prepareToPlay() always treats every slot as "changed" is unnecessary
    // in practice (prepare() itself builds directly from the current
    // values), but syncAllSlotInstancesFromParams() before the slot host has
    // ever been prepared is still a safe no-op either way.
    std::array<int, sg::numSlots> lastKnownSlotType;

    // True once prepareToPlay() has run at least once (slotHost.prepare()
    // has therefore built every slot's initial instance). Guards
    // applySlotTypeIfChanged() against trying to rebuild before there's
    // anything to rebuild.
    bool slotHostPrepared = false;

    // Cached from the most recent prepareToPlay(), used by
    // applySlotTypeIfChanged()/movePedal() to build a new slot instance at
    // the right sample rate/block size/channel count. Deliberately cached
    // explicitly here rather than read back from AudioProcessor::
    // getSampleRate()/getBlockSize() (which JUCE only documents as valid
    // "from processBlock()") -- same reasoning sg::NamProcessor::Impl caches
    // its own sessionSampleRate/maxBlockSize rather than querying back.
    double preparedSampleRate = 44100.0;
    int preparedBlockSize = 512;
    int preparedNumChannels = 2;

    // Pedal instances retired by a slot-type swap, held here (message-thread
    // only) until a short grace period has passed -- mirrors
    // sg::NamProcessor's own loader-thread retire-then-delete pattern (see
    // engine/src/NamProcessor.cpp), just driven by the message-thread timer
    // instead of a dedicated background thread. Never touched by, or
    // deleted on, the audio thread.
    struct RetiringPedal
    {
        std::unique_ptr<sg::PedalInstance> pedal;
        juce::uint32 retiredAtMs = 0;
    };
    std::vector<RetiringPedal> retiringPedals;
    static constexpr juce::uint32 retireGraceMs = 250; // matches sg::NamProcessor::Impl::kRetireGraceSeconds

    // Message-thread-only bookkeeping of the currently-loaded paths, mirrored
    // onto apvts.state properties so they round-trip through getState/setState.
    juce::String currentNamModelPath;
    juce::String currentIrPath;

    // Message-thread-only bookkeeping of the current preset, mirrored onto
    // apvts.state properties (currentPresetPathPropertyId/NamePropertyId) so
    // a DAW session remembers which preset it was on.
    juce::String currentPresetName;
    juce::String currentPresetPath;

    // Dirty tracking (see the class-level comment). Set true directly at the
    // model/IR-load and setSlotType/movePedal call sites; set true for a
    // plain param change by pollForDirtyParamChange() diffing against
    // dirtyBaselineValues. Cleared (along with a fresh baseline snapshot) by
    // refreshDirtyBaseline() on construction and after every successful
    // save/load. presetDirty is atomic since WebviewBridge (a different
    // object, but also message-thread-only) reads it; dirtyBaselineValues is
    // plain since only this processor's own message-thread code ever
    // touches it.
    std::atomic<bool> presetDirty { false };
    std::array<float, numParams> dirtyBaselineValues {};

    JUCE_DECLARE_WEAK_REFERENCEABLE (SimpleGuitarAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleGuitarAudioProcessor)
};
