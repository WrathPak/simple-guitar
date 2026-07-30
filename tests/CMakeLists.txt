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
)

target_link_libraries(sg_tests
  PRIVATE
    sg_engine          # brings juce_audio_basics / juce_dsp transitively (PUBLIC deps of sg_engine)
    Catch2::Catch2WithMain
)

target_compile_features(sg_tests PRIVATE cxx_std_20)

include(CTest)
include(Catch)
catch_discover_tests(sg_tests)
