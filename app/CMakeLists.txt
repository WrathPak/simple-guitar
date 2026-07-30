# SimpleGuitar — JUCE shell: processor, placeholder editor, standalone + plugin targets.
# The webview-hosted editor is a separate workstream; NEEDS_WEBVIEW2 / JUCE_WEB_BROWSER
# are enabled now so that later wave doesn't force a build-system churn.

set(SG_PLUGIN_FORMATS Standalone VST3)
if(APPLE)
    list(APPEND SG_PLUGIN_FORMATS AU)
endif()

juce_add_plugin(SimpleGuitar
    COMPANY_NAME                "Simple Guitar"
    PRODUCT_NAME                "Simple Guitar"
    BUNDLE_ID                   com.simpleguitar.SimpleGuitar
    PLUGIN_MANUFACTURER_CODE    Sgtr
    PLUGIN_CODE                 Sgtr
    FORMATS                     ${SG_PLUGIN_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx
    AU_MAIN_TYPE                kAudioUnitType_Effect
    NEEDS_WEBVIEW2              TRUE
)

target_sources(SimpleGuitar
    PRIVATE
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
)

target_compile_definitions(SimpleGuitar
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_ALSA=0
)

target_link_libraries(SimpleGuitar
    PRIVATE
        sg_engine
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_compile_features(SimpleGuitar PRIVATE cxx_std_20)

juce_generate_juce_header(SimpleGuitar)
