cmake_minimum_required(VERSION 3.24)

# tests/CMakeLists.txt is self-contained: it can be add_subdirectory'd
# by the top-level CMakeLists.txt before engine/ has landed, or before
# sg_engine is wired in, without breaking `cmake --build`. Once the engine
# workstream defines the sg_engine target, this file builds sg_tests and
# registers it with ctest; until then it's a graceful no-op.
if(NOT TARGET sg_engine)
  message(STATUS "tests: sg_engine target not found yet -- skipping test build")
  return()
endif()

include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")

add_executable(sg_tests
  engine/GainTest.cpp
  engine/NoiseGateTest.cpp
  engine/PostEqTest.cpp
  engine/IrLoaderTest.cpp
  engine/NamTest.cpp
)

target_link_libraries(sg_tests
  PRIVATE
    sg_engine          # brings juce_audio_basics / juce_dsp / juce_events etc. transitively (PUBLIC deps of sg_engine)
    Catch2::Catch2WithMain
)

target_compile_features(sg_tests PRIVATE cxx_std_20)

# Fixture for engine/NamTest.cpp: NeuralAmpModelerCore's own bundled example
# model (example_models/lstm.nam), already present on disk once
# FetchContent_MakeAvailable(NeuralAmpModelerCore) has run in the top-level
# CMakeLists.txt -- no hand-authored .nam fixture needed.
target_compile_definitions(sg_tests PRIVATE
  SG_NAM_TEST_MODEL_PATH="${neuralampmodelercore_SOURCE_DIR}/example_models/lstm.nam")

include(CTest)
include(Catch)
catch_discover_tests(sg_tests)
