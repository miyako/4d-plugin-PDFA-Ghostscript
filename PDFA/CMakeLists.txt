cmake_minimum_required(VERSION 3.15)
project(PDFA LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_VISIBILITY_PRESET default)
set(CMAKE_CXX_VISIBILITY_PRESET default)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
endif()

set(SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../4D-Plugin-SDK/4D Plugin API")

add_library(PDFA MODULE
    PDFA-4dplugin.cpp
    "${SDK_DIR}/4DPluginAPI.c"
)

target_include_directories(PDFA PRIVATE
    "${SDK_DIR}"
)

if(APPLE)
    set_source_files_properties("${SDK_DIR}/4DPluginAPI.c" PROPERTIES
        COMPILE_FLAGS "-Wno-int-conversion -Wno-multichar")

    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    find_library(COCOA_FRAMEWORK Cocoa)
    target_link_libraries(PDFA PRIVATE
        ${COREGRAPHICS_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
        ${COCOA_FRAMEWORK}
    )

    set_target_properties(PDFA PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "bundle"
        PREFIX ""
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    )

    # Post-build: copy resources into the bundle
    set(PLUGIN_BUNDLE "$<TARGET_BUNDLE_DIR:PDFA>")
    add_custom_command(TARGET PDFA POST_BUILD
        # Resources
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_BUNDLE}/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json" "${PLUGIN_BUNDLE}/Contents/Resources/manifest.json"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/resources/PDFA_def.ps" "${PLUGIN_BUNDLE}/Contents/Resources/PDFA_def.ps"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/resources/sRGB.icc" "${PLUGIN_BUNDLE}/Contents/Resources/sRGB.icc"
        # Helpers dir (gs binary will be placed here by bundle-gs.sh)
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_BUNDLE}/Contents/Helpers"
        # Frameworks dir (gs dylibs will be placed here by bundle-gs.sh)
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_BUNDLE}/Contents/Frameworks"
    )

elseif(WIN32)
    set_target_properties(PDFA PROPERTIES
        PREFIX ""
        SUFFIX ".4DX"
        LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/PDFA.def"
    )

    target_compile_options(PDFA PRIVATE /W3 /wd4267 /wd4244)

    set(PLUGIN_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PDFA-test/Plugins/PDFA/Contents")
    add_custom_command(TARGET PDFA POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_OUTPUT_DIR}/Windows64"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:PDFA>" "${PLUGIN_OUTPUT_DIR}/Windows64/PDFA.4DX"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_OUTPUT_DIR}/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json" "${PLUGIN_OUTPUT_DIR}/Resources/manifest.json"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/resources/PDFA_def.ps" "${PLUGIN_OUTPUT_DIR}/Resources/PDFA_def.ps"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/resources/sRGB.icc" "${PLUGIN_OUTPUT_DIR}/Resources/sRGB.icc"
    )
endif()
