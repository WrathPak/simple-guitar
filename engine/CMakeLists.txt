# sg_engine — realtime C++ DSP core.
# Hard rule: this module never includes JUCE UI
# or webview headers. juce_audio_basics, juce_dsp, juce_audio_formats and
# juce_events (pure DSP/data/file-format/message-loop modules -- no UI, no
# webview) are the only permitted JUCE dependencies here. juce_audio_formats
# is needed by IrLoader (juce::dsp::Convolution loads IR files through it)
# and is only ever touched off the audio thread (prepare()/requestLoad()).
# juce_events is needed by NamProcessor purely for MessageManager::callAsync
# (requestLoad()'s completion callback delivery) -- it is JUCE's message-loop
# module, not a UI module.

# --- sg_nam_core: NeuralAmpModelerCore's DSP sources, isolated in their own
# static library (see the FetchContent'd NeuralAmpModelerCore in the
# top-level CMakeLists.txt for how neuralampmodelercore_SOURCE_DIR is
# populated). Its own build (tools/CMakeLists.txt) is never add_subdirectory'd
# -- we compile its NAM/*.cpp sources directly ourselves, the same way
# tools/CMakeLists.txt does for its own dev executables.
#
# This is deliberately its OWN target rather than sources bolted directly
# onto sg_engine: NAM/*.cpp has zero JUCE dependency (only Eigen/nlohmann/
# std), and JUCE's CMake module system recompiles each JUCE module's
# amalgamated .cpp separately for every *top-level* target that ends up
# using it (sg_engine, app, sg_tests, ...) rather than sharing one compiled
# copy. Combining that with the /WHOLEARCHIVE workaround below (needed only
# for NAM's own static-initializer self-registration, see the MSVC block)
# on the whole of sg_engine.lib forces JUCE's module .obj files in
# unconditionally too, duplicating symbols that each consumer already
# compiles for itself -- LNK2005 "already defined" errors at the final
# app.exe/sg_tests.exe link step. Keeping NAM in its own JUCE-free static
# lib means /WHOLEARCHIVE only ever touches NAM's own object files.
file(GLOB NAM_SOURCES_TOP "${neuralampmodelercore_SOURCE_DIR}/NAM/*.cpp")
file(GLOB NAM_SOURCES_SUB "${neuralampmodelercore_SOURCE_DIR}/NAM/*/*.cpp")
# Resolves to 10 top-level + 3 wavenet/ files:
#   NAM/{activations,container,conv1d,convnet,dsp,get_dsp,linear,lstm,ring_buffer,util}.cpp
#   NAM/wavenet/{a2_fast,model,slimmable}.cpp
# Glob is safe here since the fetched tag is pinned (v0.5.4).

add_library(sg_nam_core STATIC
    ${NAM_SOURCES_TOP}
    ${NAM_SOURCES_SUB}
)

# Include paths NAM's own headers need, all PUBLIC so both NAM's own sources
# and anything linking sg_nam_core (i.e. sg_engine's NamProcessor.cpp) see
# them; marked SYSTEM so warnings in this vendored code don't affect our own
# warning settings.
#   ${neuralampmodelercore_SOURCE_DIR}        for `#include <NAM/dsp.h>` etc (matches
#                                              upstream tools/CMakeLists.txt's `include_directories(tools ..)`)
#   .../Dependencies/eigen                    for `#include <Eigen/Dense>`
#   .../Dependencies/nlohmann                 for `#include "json.hpp"` -- this is the
#                                              top-level Dependencies/nlohmann (vendored
#                                              directly in the NAM repo, see its own
#                                              Dependencies/info.txt), which is what
#                                              upstream's own tools/CMakeLists.txt points at
#                                              via NAM_DEPS_PATH/nlohmann. NOT the nested
#                                              Dependencies/AudioDSPTools/Dependencies/nlohmann
#                                              copy -- NamProcessor/NAM headers don't use
#                                              AudioDSPTools at all, so that dependency isn't
#                                              needed here.
target_include_directories(sg_nam_core SYSTEM PUBLIC
    ${neuralampmodelercore_SOURCE_DIR}
    ${neuralampmodelercore_SOURCE_DIR}/Dependencies/eigen
    ${neuralampmodelercore_SOURCE_DIR}/Dependencies/nlohmann
)

target_compile_definitions(sg_nam_core PUBLIC
    NAM_ENABLE_A2_FAST # matches upstream's own default (its CMakeLists.txt option is ON)
)

target_compile_features(sg_nam_core PUBLIC cxx_std_20)

set_target_properties(sg_nam_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

if (WIN32)
    # Matches upstream NeuralAmpModelerCore's own Windows handling (avoids
    # windows.h's min/max macros colliding with std::min/std::max and Eigen).
    target_compile_definitions(sg_nam_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if (MSVC)
    # WaveNet/LSTM/ConvNet/SlimmableContainer register themselves into NAM's
    # runtime architecture registry via static initializers (see NAM/registry.h,
    # e.g. `static nam::ConfigParserHelper _register_LSTM(...)` in NAM/lstm.cpp)
    # in translation units that nothing else ever references by symbol --
    # get_dsp()/dsp.h only look them up later by string name through that
    # registry, never by naming the concrete C++ types. MSVC's static-lib
    # linker only pulls a .obj member from a .lib into the final link if
    # something already-included references a symbol defined in it, so
    # without this, those four .obj files (and their self-registrations)
    # would silently be dropped from app.exe/sg_tests.exe, and get_dsp()
    # would throw "no parser registered for architecture: ..." at runtime
    # for every real-world model. INTERFACE here means it propagates all the
    # way to whatever final executable ends up linking sg_nam_core in
    # (transitively, through sg_engine), even though sg_engine only needs to
    # link sg_nam_core PRIVATEly.
    target_link_options(sg_nam_core INTERFACE "/WHOLEARCHIVE:$<TARGET_FILE_NAME:sg_nam_core>")
elseif(APPLE)
    # Same static-initializer problem, Apple linker spelling: without
    # -force_load, ld strips the unreferenced NAM objects and their
    # architecture self-registrations, and get_dsp() throws "No config
    # parser registered for architecture: ..." at runtime (caught by CI on
    # macos-latest; Windows was already covered by /WHOLEARCHIVE above).
    target_link_options(sg_nam_core INTERFACE
        "SHELL:-Wl,-force_load,$<TARGET_FILE:sg_nam_core>")
else()
    # ELF linkers: same idea via --whole-archive.
    target_link_options(sg_nam_core INTERFACE
        "SHELL:-Wl,--whole-archive,$<TARGET_FILE:sg_nam_core>,--no-whole-archive")
endif()

# --- sg_engine -----------------------------------------------------------

add_library(sg_engine STATIC
    src/Gain.cpp
    src/MeterTap.cpp
    src/NoiseGate.cpp
    src/PostEq.cpp
    src/IrLoader.cpp
    src/NamProcessor.cpp
)

target_include_directories(sg_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(sg_engine
    PUBLIC
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_events
    PRIVATE
        sg_nam_core # implementation detail of NamProcessor.cpp; not exposed in sg_engine's public headers
)

target_compile_definitions(sg_engine PUBLIC
    JUCE_MODAL_LOOPS_PERMITTED=1  # required for MessageManager::runDispatchLoopUntil, used by
                                  # tests/engine/NamTest.cpp to synchronously observe NamProcessor's
                                  # async requestLoad() callback in a non-GUI test binary. JUCE's CMake
                                  # module system recompiles each module's amalgamated .cpp per
                                  # consumer target, so PUBLIC here is what makes it apply consistently
                                  # both where sg_engine itself compiles juce_events and where
                                  # sg_tests/app do too.
)

target_compile_features(sg_engine PUBLIC cxx_std_20)

set_target_properties(sg_engine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
