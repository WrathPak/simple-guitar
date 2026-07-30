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
  engine/BundledNamTest.cpp
  engine/ScreamerTest.cpp
  engine/StereoDelayTest.cpp
  engine/PlateReverbTest.cpp
  engine/CompressorTest.cpp
  engine/ChorusTest.cpp
  engine/TremoloTest.cpp
  app/PedalSlotsTest.cpp
  app/StateMigrationTest.cpp
  app/BundledPresetsTest.cpp
  app/ContentInstallerTest.cpp
  app/PluginHostingCoreTest.cpp
  ../app/source/PresetStore.cpp
  ../app/source/ContentInstallerCore.cpp
  ../app/source/PedalSlots.cpp
  ../app/source/StateMigration.cpp
  ../app/source/PluginScanCore.cpp
  ../app/source/PluginSlotCore.cpp
)

target_link_libraries(sg_tests
  PRIVATE
    sg_engine          # brings juce_audio_basics / juce_dsp / juce_events etc. transitively (PUBLIC deps of sg_engine)
    Catch2::Catch2WithMain
)

# app/PedalSlotsTest.cpp and app/StateMigrationTest.cpp exercise
# app/source/PedalSlots.{h,cpp} and app/source/StateMigration.{h,cpp}
# directly -- neither pulls in JUCE UI/webview headers, so this test binary
# includes/links them straight from app/source/ rather than linking the whole
# app target (which pulls in juce_gui_extra/webview deps sg_tests has no
# business depending on).
target_include_directories(sg_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../app/source
)

target_compile_features(sg_tests PRIVATE cxx_std_20)

# Fixture for engine/NamTest.cpp: NeuralAmpModelerCore's own bundled example
# model (example_models/lstm.nam), already present on disk once
# FetchContent_MakeAvailable(NeuralAmpModelerCore) has run in the top-level
# CMakeLists.txt -- no hand-authored .nam fixture needed.
#
# Fixtures for the content-verification tests (engine/BundledNamTest.cpp,
# app/BundledPresetsTest.cpp): the committed content/ directory itself, read
# directly off disk rather than through the built content zip/BinaryData --
# these tests validate the actual source-of-truth files the build embeds.
target_compile_definitions(sg_tests PRIVATE
  SG_NAM_TEST_MODEL_PATH="${neuralampmodelercore_SOURCE_DIR}/example_models/lstm.nam"
  SG_CONTENT_MODELS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../content/models"
  SG_CONTENT_IRS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../content/irs"
  SG_CONTENT_PRESETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../content/presets")

include(CTest)
include(Catch)
catch_discover_tests(sg_tests)
