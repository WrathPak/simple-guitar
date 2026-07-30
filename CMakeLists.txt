cmake_minimum_required(VERSION 3.24)

project(SimpleGuitar VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Speeds up multi-config generators (Visual Studio) and keeps configs sane.
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(JUCE)

# NAM core -- NeuralAmpModelerCore (MIT), the reference NAM DSP implementation
# used by engine/src/NamProcessor.cpp. Its own top-level CMakeLists.txt does not
# expose a reusable library target -- it only builds internal dev tools
# (loadmodel/benchmodel/render/run_tests) via add_subdirectory(tools), which pulls
# in things we don't want here. We only need its source tree, so SOURCE_SUBDIR
# points at NAM/, which has no CMakeLists.txt of its own: FetchContent_MakeAvailable
# still populates the full repository (so example_models/, used by the test suite,
# is available on disk) but never add_subdirectory()'s -- and therefore never
# builds -- the upstream project. engine/CMakeLists.txt compiles the NAM sources
# (globbed from ${neuralampmodelercore_SOURCE_DIR}/NAM) directly into sg_engine.
#
# The only submodule NAM's own sources need is Dependencies/eigen (header-only
# Eigen, used throughout NAM's DSP code). NAM/*.h's `#include "json.hpp"`
# resolves against Dependencies/nlohmann/json.hpp, which is vendored directly
# in the main repository (see Dependencies/info.txt) rather than as a
# submodule, so it comes along with the regular clone with nothing extra to
# fetch.
FetchContent_Declare(
    NeuralAmpModelerCore
    GIT_REPOSITORY   https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG          v0.5.4
    GIT_SUBMODULES   "Dependencies/eigen"
    GIT_SUBMODULES_RECURSE FALSE
    GIT_SHALLOW      TRUE
    GIT_PROGRESS     TRUE
    SOURCE_SUBDIR    NAM
)
FetchContent_MakeAvailable(NeuralAmpModelerCore)

# engine/ — realtime C++ DSP core, no JUCE UI/webview dependencies.
add_subdirectory(engine)

# app/ — JUCE shell: processor, editor, standalone/plugin targets.
add_subdirectory(app)

# tests/ is owned by a separate workstream; only wire it in once it exists.
# enable_testing() must live at the root so `ctest --test-dir build` finds them.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    enable_testing()
    add_subdirectory(tests)
endif()
