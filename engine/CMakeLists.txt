# sg_engine — realtime C++ DSP core.
# Hard rule: this module never includes JUCE UI
# or webview headers. juce_audio_basics and juce_dsp (pure DSP/data modules)
# are the only permitted JUCE dependencies here.

add_library(sg_engine STATIC
    src/Gain.cpp
    src/MeterTap.cpp
)

target_include_directories(sg_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(sg_engine
    PUBLIC
        juce::juce_audio_basics
        juce::juce_dsp
)

target_compile_features(sg_engine PUBLIC cxx_std_20)

set_target_properties(sg_engine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
