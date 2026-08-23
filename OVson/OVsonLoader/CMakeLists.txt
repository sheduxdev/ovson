cmake_minimum_required(VERSION 3.21)
project(OVsonLoader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use the pre-installed Slint C++ release. Adjust SLINT_INSTALL_ROOT
# if you installed Slint to a different location. The installer drops
# a CMake config file under lib/cmake/Slint that exposes the
# Slint::Slint target + slint_target_sources() helper.
set(SLINT_INSTALL_ROOT "C:/Program Files/Slint-cpp 1.16.1"
    CACHE PATH "Slint C++ install root")
list(APPEND CMAKE_PREFIX_PATH "${SLINT_INSTALL_ROOT}")

find_package(Slint 1.16.1 CONFIG QUIET)
if(NOT Slint_FOUND)
    message(FATAL_ERROR
        "OVsonLoader requires Slint C++ 1.16.1. Set Slint_DIR to the directory "
        "containing SlintConfig.cmake, set SLINT_INSTALL_ROOT to the Slint "
        "installation root, or configure with -DOVSON_BUILD_LOADER=OFF to "
        "build only the OVson DLL and deterministic tests.")
endif()

# Everything UI-related lives in slint_ui/. The old UI/, Injection/
# and Utils/ folders are left in place but no longer compiled — they
# carry the GDI+ loader history if you want to look back.
add_executable(OVsonLoader WIN32
    slint_ui/main.cpp
    slint_ui/crash_dumps.cpp
    slint_ui/injector.cpp
    slint_ui/log_tail.cpp
    slint_ui/process_scan.cpp
    slint_ui/settings.cpp
    slint_ui/shutdown_coordinator.cpp
    slint_ui/tray_icon.cpp
    slint_ui/updater.cpp
    OVsonLoader.rc
)

# slint_target_sources runs the .slint → C++ codegen step. The
# generated header is dropped into the target's include search path
# and is then #include'd from main.cpp as "ovson_ui.h".
slint_target_sources(OVsonLoader slint_ui/ovson_ui.slint)

# The UI imports the Sen .ttf files. Without this the generated code points at
# the fonts by absolute build-machine path and they are missing on every other
# machine; embed-files bakes the bytes into the executable instead.
set_property(TARGET OVsonLoader PROPERTY SLINT_EMBED_RESOURCES embed-files)

# OVsonLoader.rc embeds five RCDATA payloads: the OVson.dll that gets injected,
# the Slint runtime the loader drops next to itself, and three documents. Two of
# them only exist once their producing target has linked, so stage the whole set
# into one directory and hand that directory to the resource compiler. The .rc
# then names them plainly and needs no knowledge of the build layout.
set(OVSON_RCDATA_DIR "${CMAKE_CURRENT_BINARY_DIR}/rcdata")
set(OVSON_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

configure_file("${OVSON_REPO_ROOT}/LICENSE"
               "${OVSON_RCDATA_DIR}/LICENSE.txt" COPYONLY)
configure_file("${OVSON_REPO_ROOT}/README.md"
               "${OVSON_RCDATA_DIR}/readme_embed.txt" COPYONLY)
configure_file("${OVSON_REPO_ROOT}/CHANGES_FOR_UPSTREAM.md"
               "${OVSON_RCDATA_DIR}/changelog_embed.txt" COPYONLY)

add_custom_command(
    OUTPUT "${OVSON_RCDATA_DIR}/OVson.dll" "${OVSON_RCDATA_DIR}/slint_cpp.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OVson> "${OVSON_RCDATA_DIR}/OVson.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SLINT_INSTALL_ROOT}/lib/slint_cpp.dll"
            "${OVSON_RCDATA_DIR}/slint_cpp.dll"
    DEPENDS OVson "${SLINT_INSTALL_ROOT}/lib/slint_cpp.dll"
    COMMENT "Staging OVsonLoader's RCDATA payloads"
    VERBATIM)
add_custom_target(OVsonLoaderRcData
    DEPENDS "${OVSON_RCDATA_DIR}/OVson.dll" "${OVSON_RCDATA_DIR}/slint_cpp.dll")
add_dependencies(OVsonLoader OVsonLoaderRcData)

target_include_directories(OVsonLoader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/slint_ui
    ${OVSON_RCDATA_DIR}
)

# Block the Windows <minwindef.h> min/max macros — they collide with
# std::max/min used heavily inside Slint's templated headers and
# produce dozens of unreadable parse errors.
target_compile_definitions(OVsonLoader PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(OVsonLoader PRIVATE
    Slint::Slint
    User32.lib
    Shell32.lib
    delayimp.lib
)

# main.cpp writes the embedded slint_cpp.dll (RCDATA 2) to %TEMP% and
# LoadLibrary's it before the first Slint call. That only buys anything if the
# import is delay-loaded: a normal import is resolved by the OS loader before
# wWinMain runs, so the exe would refuse to start with 0xC0000135 unless the
# DLL happened to sit next to it. With this the executable is self-contained.
target_link_options(OVsonLoader PRIVATE /DELAYLOAD:slint_cpp.dll)

set_target_properties(OVsonLoader PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Copy the Slint runtime DLLs next to the EXE after every build so the
# loader actually starts when launched outside of Visual Studio.
if(WIN32)
    add_custom_command(TARGET OVsonLoader POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:OVsonLoader>
            $<TARGET_FILE_DIR:OVsonLoader>
        COMMAND_EXPAND_LISTS)
endif()
