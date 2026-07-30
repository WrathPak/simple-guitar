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
