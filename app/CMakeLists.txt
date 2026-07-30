# SimpleGuitar — JUCE shell: processor, webview-hosted editor, standalone + plugin targets.
# NEEDS_WEBVIEW2 / JUCE_WEB_BROWSER enable the WebBrowserComponent editor that serves
# the built React UI (ui/dist), zipped and embedded as BinaryData at build time.

set(SG_PLUGIN_FORMATS Standalone VST3)
if(APPLE)
    list(APPEND SG_PLUGIN_FORMATS AU)
endif()

# ---------------------------------------------------------------------------
# UI embedding: build ui/ (npm run build -> ui/dist), zip the dist folder, and
# embed the zip as BinaryData. The editor (PluginEditor.cpp) serves it back
# out through a WebBrowserComponent resource provider + juce::ZipFile, mirroring
# JUCE's own examples/Plugins/WebViewPluginDemo.h reference pattern.
set(SG_UI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ui")
set(SG_UI_DIST_DIR "${SG_UI_DIR}/dist")
set(SG_UI_DIST_STAMP "${SG_UI_DIST_DIR}/index.html")
set(SG_UI_ZIP "${CMAKE_CURRENT_BINARY_DIR}/webview/uidist.zip")

find_program(SG_NPM_EXECUTABLE NAMES npm npm.cmd)
if(NOT SG_NPM_EXECUTABLE)
    message(FATAL_ERROR "npm not found on PATH -- required to build ui/ (see app/CMakeLists.txt)")
endif()

# Re-run `npm run build` whenever any UI source file is newer than the last
# build output. CONFIGURE_DEPENDS re-globs on each build-system regeneration
# so newly added files are picked up on the next `cmake --build`/reconfigure.
file(GLOB_RECURSE SG_UI_SOURCE_FILES CONFIGURE_DEPENDS
    "${SG_UI_DIR}/src/*"
    "${SG_UI_DIR}/public/*"
)
list(APPEND SG_UI_SOURCE_FILES
    "${SG_UI_DIR}/index.html"
    "${SG_UI_DIR}/package.json"
    "${SG_UI_DIR}/vite.config.ts"
    "${SG_UI_DIR}/tsconfig.json"
)

add_custom_command(
    OUTPUT "${SG_UI_DIST_STAMP}"
    COMMAND "${SG_NPM_EXECUTABLE}" run build
    WORKING_DIRECTORY "${SG_UI_DIR}"
    DEPENDS ${SG_UI_SOURCE_FILES}
    COMMENT "ui: npm run build (React app -> ui/dist)"
    VERBATIM
)

# -E chdir (rather than WORKING_DIRECTORY) so a missing ui/dist fails the build
# loudly instead of silently producing no zip.
add_custom_command(
    OUTPUT "${SG_UI_ZIP}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/webview"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${SG_UI_DIST_DIR}"
            "${CMAKE_COMMAND}" -E tar "cf" "${SG_UI_ZIP}" --format=zip .
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS "${SG_UI_DIST_STAMP}"
    COMMENT "ui: zipping ui/dist -> ${SG_UI_ZIP}"
    VERBATIM
)

juce_add_binary_data(SimpleGuitarUiData SOURCES "${SG_UI_ZIP}")

# Strict ordering. The Visual Studio generator does not reliably serialize
# chained custom commands that land in the same project (they can run out of
# order or in parallel on a clean one-shot build, so the zip step can race
# vite's dist clean, and the BinaryData generation can run before the zip
# exists). Giving each custom command its own target and chaining them with
# add_dependencies forces project-level ordering, which MSBuild does honor:
#   sg_ui_build (npm run build) -> sg_ui_bundle (zip) -> SimpleGuitarUiData.
add_custom_target(sg_ui_build DEPENDS "${SG_UI_DIST_STAMP}")
add_custom_target(sg_ui_bundle DEPENDS "${SG_UI_ZIP}")
add_dependencies(sg_ui_bundle sg_ui_build)
add_dependencies(SimpleGuitarUiData sg_ui_bundle)

# ---------------------------------------------------------------------------
# Factory content embedding: zip content/ (models/*.nam, irs/*.wav,
# presets/*.sgpreset, ATTRIBUTION.md -- see content/ATTRIBUTION.md) and embed
# it as BinaryData, same mechanism as the ui/dist embedding above. Content is
# static/committed (no npm-style build step needed first), so this only needs
# the zip-then-embed half of that pattern; strict ordering is still enforced
# with its own custom target (see the ui block's comment for why chained
# custom commands landing in the same project can't be trusted to
# self-order on the Visual Studio generator).
set(SG_CONTENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../content")
set(SG_CONTENT_ZIP "${CMAKE_CURRENT_BINARY_DIR}/content/sgcontent.zip")

file(GLOB_RECURSE SG_CONTENT_SOURCE_FILES CONFIGURE_DEPENDS
    "${SG_CONTENT_DIR}/*"
)

add_custom_command(
    OUTPUT "${SG_CONTENT_ZIP}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/content"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${SG_CONTENT_DIR}"
            "${CMAKE_COMMAND}" -E tar "cf" "${SG_CONTENT_ZIP}" --format=zip .
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS ${SG_CONTENT_SOURCE_FILES}
    COMMENT "content: zipping content/ -> ${SG_CONTENT_ZIP}"
    VERBATIM
)

# Distinct NAMESPACE/HEADER_NAME (default for both is BinaryData/BinaryData.h):
# a second juce_add_binary_data target using the defaults would generate its
# own same-named BinaryData.h in the "BinaryData" namespace, which collides
# with SimpleGuitarUiData's own BinaryData.h the moment both targets' include
# dirs land on the same consuming target (SimpleGuitar) -- #include <BinaryData.h>
# would then resolve to whichever one wins the include-path race, not
# necessarily the one a given .cpp actually wants.
juce_add_binary_data(SimpleGuitarContentData
    NAMESPACE   ContentBinaryData
    HEADER_NAME ContentBinaryData.h
    SOURCES     "${SG_CONTENT_ZIP}"
)

add_custom_target(sg_content_bundle DEPENDS "${SG_CONTENT_ZIP}")
add_dependencies(SimpleGuitarContentData sg_content_bundle)

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
        source/WebviewBridge.cpp
        source/RigLibrary.cpp
        source/PresetStore.cpp
        source/ContentInstaller.cpp
        source/ContentInstallerCore.cpp
        source/PedalSlots.cpp
        source/StateMigration.cpp
        source/PluginScanCore.cpp
        source/PluginSlotCore.cpp
        source/PluginHosting.cpp
)

# JUCE_PLUGINHOST_VST3: M3 hosts third-party VST3 plugins inside the pedal
# chain (see source/PluginHosting.h).
target_compile_definitions(SimpleGuitar
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_ALSA=0
        JUCE_PLUGINHOST_VST3=1
)

# The Standalone target swaps JUCE's stock StandaloneFilterApp for our own
# (source/StandaloneApp.cpp): identical normal behavior plus the hidden
# --scan-plugin child-process mode the out-of-process plugin scanner
# launches per candidate (see source/PluginHosting.h). Standalone-only: the
# other formats never scan out-of-process themselves.
target_compile_definitions(SimpleGuitar_Standalone
    PRIVATE
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
)
target_sources(SimpleGuitar_Standalone
    PRIVATE
        source/StandaloneApp.cpp
)

if(WIN32)
    # Required for WebBrowserComponent::Options::Backend::webview2, the
    # resource provider (withResourceProvider), and the statically-linked
    # WebView2LoaderStatic.lib pulled in by NEEDS_WEBVIEW2 above -- mirrors
    # JUCE's own WebViewPluginDemo PIP moduleFlags
    # (JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1), which the Projucer/PIP
    # exporters set automatically but plain CMake does not.
    target_compile_definitions(SimpleGuitar
        PUBLIC
            JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    )
endif()

target_link_libraries(SimpleGuitar
    PRIVATE
        sg_engine
        SimpleGuitarUiData
        SimpleGuitarContentData
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
