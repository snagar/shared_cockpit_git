# -----------------------------------------------------------------------------
#  shared_cockpit - X-Plane plugin (Dear ImGui via ImgWindow)
#
#  Layout:
#     CMakeLists.txt
#     src/                 plugin frame (entry points, main window, shared values)
#     src/<screen>/        one folder per screen: home/, analyse/, connect/, flightdeck/
#     src/shared/          code used by several screens: analysis, store, log, ui, util, xp
#     tools/               command-line tests: analysis, network (no X-Plane needed)
#     libs/xpsdk/          X-Plane SDK 4.x (unpack libs/XPSDK.zip here yourself)
#     libs/imgui/          Dear ImGui
#     libs/ImgWindow/      ImgWindow
#     libs/loguru/         loguru logging
#     libs/fmt/            {fmt} (include/ + src/)
#     libs/json/           nlohmann/json (include/nlohmann/json.hpp)
#     libs/enet/           ENet 1.3.18 networking (*.c, include/enet/, LICENSE)
#     libs/fonts/          IconsFontAwesome5.h + fa-solid-900.inc
#     libs/sc_manip_types.ini   shipped next to the plugin
#
#  Build (Linux, Windows, macOS - the same commands):
#     cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#     cmake --build build --config Release -j
#  Result: <out>/shared_cockpit/<lin_x64|win_x64|mac_x64>/shared_cockpit.xpl
#          <out>/shared_cockpit/sc_manip_types.ini
#          <out> = the build folder (Makefiles / Ninja), or build/<config> (Visual Studio / Xcode)
#  Install:  cmake --install build --prefix "<X-Plane>/Resources/plugins"
#  Test tools (SC_BUILD_TOOLS, ON by default): <out>/tools/sc_analyse_test and <out>/tools/sc_net_test
# -----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.20)

# macOS: one plugin file for Apple Silicon and Intel Macs.
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS: architectures in the plugin")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS: oldest supported macOS version")

# Windows: link the C/C++ runtime into the plugin (/MT): no Visual C++ redistributable needed.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The project name is the plugin id (folder and .xpl name). It must stay the same as
# info::kPluginId in src/SharedInfo.h. The version is in src/SharedInfo.h, not here.
project(shared_cockpit
    DESCRIPTION "Shared Cockpit - X-Plane plugin"
    LANGUAGES C CXX)

if(WIN32)
    set(SC_PLATFORM_DIR "win_x64")
elseif(APPLE)
    set(SC_PLATFORM_DIR "mac_x64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SC_PLATFORM_DIR "lin_x64")
else()
    message(FATAL_ERROR "${PROJECT_NAME} builds on Linux, Windows and macOS only.")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- Where the results go -------------------------------------------------------------
# Single-config generators (Makefiles, Ninja): one build folder per type, straight inside it:
#     cmake -S . -B build/debug   -DCMAKE_BUILD_TYPE=Debug    -> build/debug/tools/sc_net_test
#     cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release  -> build/release/tools/sc_net_test
# Multi-config (Visual Studio, Xcode): one build folder, a subfolder per type (build/Debug/...).
get_property(_sc_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_sc_multi_config)
    set(SC_OUT_DIR "${CMAKE_BINARY_DIR}/$<CONFIG>")
else()
    set(SC_OUT_DIR "${CMAKE_BINARY_DIR}")
endif()
# The static helper libraries (fmt, enet, imgui, core, ...) go to <out>/lib.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${SC_OUT_DIR}/lib")


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)       # everything ends up in a shared object
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

option(SC_BUILD_TOOLS "Also build the command-line test tools (tools/)" ON)

# --- Paths ------------------------------------------------------------------------
set(SC_LIBS_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/libs"  CACHE PATH "Third-party libraries root")
set(IMGUI_DIR      "${SC_LIBS_DIR}/imgui"              CACHE PATH "Dear ImGui root")
set(IMGWINDOW_DIR  "${SC_LIBS_DIR}/ImgWindow"          CACHE PATH "ImgWindow root")
set(LOGURU_DIR     "${SC_LIBS_DIR}/loguru"             CACHE PATH "loguru root")
set(FMT_DIR        "${SC_LIBS_DIR}/fmt"                CACHE PATH "{fmt} root (include/ and src/)")
set(JSON_DIR       "${SC_LIBS_DIR}/json"               CACHE PATH "nlohmann/json root (include/)")
set(FONTS_DIR      "${SC_LIBS_DIR}/fonts"              CACHE PATH "Embedded fonts")
set(ENET_DIR       "${SC_LIBS_DIR}/enet"               CACHE PATH "ENet root (*.c and include/enet/)")

foreach(_check
        "${FMT_DIR}/include/fmt/format.h"
        "${FMT_DIR}/src/format.cc"
        "${JSON_DIR}/include/nlohmann/json.hpp"
        "${ENET_DIR}/include/enet/enet.h"
        "${ENET_DIR}/host.c"
        "${LOGURU_DIR}/loguru.cpp")
    if(NOT EXISTS "${_check}")
        message(FATAL_ERROR "Missing dependency: ${_check}")
    endif()
endforeach()

# --- {fmt} (static) -----------------------------------------------------------------
add_library(sc_fmt STATIC "${FMT_DIR}/src/format.cc" "${FMT_DIR}/src/os.cc")
target_include_directories(sc_fmt SYSTEM PUBLIC "${FMT_DIR}/include")
target_compile_options(sc_fmt PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w /utf-8,-w>)
if(MSVC)
    target_compile_options(sc_fmt PUBLIC /utf-8)              # {fmt} requires UTF-8 sources on MSVC
endif()

# --- nlohmann/json (header only) -------------------------------------------------------
add_library(sc_json INTERFACE)
target_include_directories(sc_json SYSTEM INTERFACE "${JSON_DIR}/include")

# --- ENet (static, built from source with the plugin's own settings) -------------------
# Built here rather than linked pre-built, so it always matches the plugin: /MT on Windows,
# arm64 + x86_64 and the deployment target on macOS, position-independent code on Linux.
# unix.c and win32.c are both listed: each compiles only on its own platform (#ifdef _WIN32).
add_library(sc_enet STATIC
    "${ENET_DIR}/callbacks.c" "${ENET_DIR}/compress.c" "${ENET_DIR}/host.c" "${ENET_DIR}/list.c"
    "${ENET_DIR}/packet.c" "${ENET_DIR}/peer.c" "${ENET_DIR}/protocol.c" "${ENET_DIR}/unix.c" "${ENET_DIR}/win32.c")
# The include folder, not include/enet: ENet has its own time.h, which must not hide the system one.
target_include_directories(sc_enet SYSTEM PUBLIC "${ENET_DIR}/include")
target_compile_options(sc_enet PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/w,-w>)
if(WIN32)
    # Winsock and the multimedia timer: ENet's code has no #pragma comment(lib), so link them here.
    target_link_libraries(sc_enet PUBLIC ws2_32 winmm)
else()
    # The same "configure" checks as ENet's own CMakeLists.txt.
    include(CheckFunctionExists)
    include(CheckStructHasMember)
    include(CheckTypeSize)
    foreach(_fn fcntl poll getaddrinfo getnameinfo gethostbyname_r gethostbyaddr_r inet_pton inet_ntop)
        string(TOUPPER "HAS_${_fn}" _var)
        check_function_exists("${_fn}" ${_var})
        if(${_var})
            target_compile_definitions(sc_enet PRIVATE ${_var}=1)
        endif()
    endforeach()
    check_struct_has_member("struct msghdr" "msg_flags" "sys/types.h;sys/socket.h" HAS_MSGHDR_FLAGS)
    if(HAS_MSGHDR_FLAGS)
        target_compile_definitions(sc_enet PRIVATE HAS_MSGHDR_FLAGS=1)
    endif()
    set(CMAKE_EXTRA_INCLUDE_FILES "sys/types.h" "sys/socket.h")
    check_type_size("socklen_t" HAS_SOCKLEN_T BUILTIN_TYPES_ONLY)
    unset(CMAKE_EXTRA_INCLUDE_FILES)
    if(HAS_SOCKLEN_T)
        target_compile_definitions(sc_enet PRIVATE HAS_SOCKLEN_T=1)
    endif()
endif()

# --- loguru (static) -------------------------------------------------------------
add_library(loguru STATIC "${LOGURU_DIR}/loguru.cpp")
target_include_directories(loguru SYSTEM PUBLIC "${LOGURU_DIR}")
target_compile_definitions(loguru PUBLIC LOGURU_WITH_STREAMS=0 LOGURU_USE_FMTLIB=0)
target_compile_options(loguru PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)
find_package(Threads REQUIRED)
target_link_libraries(loguru PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

# --- The core: no X-Plane, shared by the plugin and the test tools ------------------------
# Analysis, store, log and network: everything the command-line tools need too.
set(SC_CORE_SOURCES
    src/shared/util/Utils.cpp
    src/shared/util/Utils.h
    src/shared/util/StopThread.h
    src/shared/analysis/Analysis.cpp
    src/shared/analysis/Analysis.h
    src/shared/analysis/ManipTypes.cpp
    src/shared/analysis/ManipTypes.h
    src/shared/analysis/ObjScan.cpp
    src/shared/analysis/ObjScan.h
    src/shared/analysis/Builder.cpp
    src/shared/analysis/Builder.h
    src/shared/store/AnalysisStore.cpp
    src/shared/store/AnalysisStore.h
    src/shared/store/JsonAnalysisStore.cpp
    src/shared/log/Log.cpp
    src/shared/log/Log.h
    src/shared/net/Protocol.h
    src/shared/net/Messages.cpp
    src/shared/net/Messages.h
    src/shared/net/NetThread.cpp
    src/shared/net/NetThread.h
    src/shared/net/NetSession.cpp
    src/shared/net/NetSession.h
    src/shared/net/SessionRules.h
    src/shared/net/SessionHub.cpp
    src/shared/net/SessionHub.h
    src/shared/net/LocalAddresses.cpp
    src/shared/net/LocalAddresses.h
    src/shared/settings/Settings.cpp
    src/shared/settings/Settings.h)
add_library(sc_core STATIC ${SC_CORE_SOURCES})
target_include_directories(sc_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(sc_core PUBLIC sc_fmt loguru PRIVATE sc_json sc_enet)
if(MSVC)
    # /Zc:preprocessor: the standard preprocessor (the LOG_* macros use __VA_OPT__), so PUBLIC.
    target_compile_options(sc_core PRIVATE /W4 /permissive- /Zc:__cplusplus PUBLIC /Zc:preprocessor)
    # enet.h includes winsock2.h / windows.h: keep min/max usable.
    target_compile_definitions(sc_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(sc_core PRIVATE -Wall -Wextra -Wshadow)
endif()
if(WIN32)
    # LocalAddresses: GetAdaptersAddresses.
    target_link_libraries(sc_core PRIVATE iphlpapi)
endif()

if(SC_BUILD_TOOLS)
    add_executable(sc_analyse_test tools/sc_analyse_test.cpp)
    target_link_libraries(sc_analyse_test PRIVATE sc_core)
    # The network test: host / join from a console menu, or --selftest (all in one process).
    add_executable(sc_net_test tools/sc_net_test.cpp)
    target_link_libraries(sc_net_test PRIVATE sc_core)
    # Their own folder, next to the plugin's: <out>/tools/ (nothing is copied anywhere).
    set_target_properties(sc_analyse_test sc_net_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${SC_OUT_DIR}/tools")
endif()

# --- X-Plane SDK ---------------------------------------------------------------------
# Expected unpacked in libs/xpsdk/ (CHeaders/, Libraries/), or pass -DXPSDK_DIR=<folder>.
# CMake does not unpack libs/XPSDK.zip: without the folder the configuration stops.
set(XPSDK_DIR "${SC_LIBS_DIR}/xpsdk" CACHE PATH "X-Plane SDK folder (holds CHeaders/ and Libraries/)")
message(STATUS "${PROJECT_NAME}: X-Plane SDK ${XPSDK_DIR}")
if(NOT EXISTS "${XPSDK_DIR}/CHeaders/XPLM/XPLMDefs.h")
    message(FATAL_ERROR "No X-Plane SDK in ${XPSDK_DIR} (CHeaders/XPLM/XPLMDefs.h is missing). "
                        "Unpack libs/XPSDK.zip so that its SDK/ folder becomes libs/xpsdk, or pass -DXPSDK_DIR=<folder>.")
endif()

foreach(_check
        "${IMGUI_DIR}/imgui.h"
        "${IMGWINDOW_DIR}/ImgWindow.h"
        "${FONTS_DIR}/IconsFontAwesome5.h"
        "${FONTS_DIR}/fa-solid-900.inc")
    if(NOT EXISTS "${_check}")
        message(FATAL_ERROR "Missing dependency: ${_check}")
    endif()
endforeach()

add_library(xpsdk INTERFACE)
target_include_directories(xpsdk SYSTEM INTERFACE
    "${XPSDK_DIR}/CHeaders/XPLM"
    "${XPSDK_DIR}/CHeaders/Widgets")
# X-Plane 11.50+ API levels, as in vrconfig_helper. Do NOT add XPLM440: ImgWindow's Panel
# Graphics bridge relies on it being undefined (it finds X-Plane 12's functions at run time).
target_compile_definitions(xpsdk INTERFACE
    XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1
    APL=$<BOOL:${APPLE}> IBM=$<BOOL:${WIN32}> LIN=$<STREQUAL:${SC_PLATFORM_DIR},lin_x64>)
if(WIN32)
    set(_sc_xplm_lib "${XPSDK_DIR}/Libraries/Win/XPLM_64.lib")
    if(NOT EXISTS "${_sc_xplm_lib}")
        message(FATAL_ERROR "Missing ${_sc_xplm_lib} (is it in the SDK zip?)")
    endif()
    target_link_libraries(xpsdk INTERFACE "${_sc_xplm_lib}")
elseif(APPLE)
    # Linked by its binary, so a zip that stored the framework's symlinks as files still works.
    file(GLOB _sc_xplm_bins "${XPSDK_DIR}/Libraries/Mac/XPLM.framework/Versions/*/XPLM")
    set(_sc_xplm_lib "")
    foreach(_bin IN LISTS _sc_xplm_bins)
        if(NOT IS_SYMLINK "${_bin}" AND NOT IS_DIRECTORY "${_bin}")
            file(SIZE "${_bin}" _size)
            if(_size GREATER 1000)
                set(_sc_xplm_lib "${_bin}")
            endif()
        endif()
    endforeach()
    if(NOT _sc_xplm_lib)
        message(FATAL_ERROR "No XPLM framework binary under ${XPSDK_DIR}/Libraries/Mac")
    endif()
    target_link_libraries(xpsdk INTERFACE "${_sc_xplm_lib}")
endif()
# Linux: XPLM symbols stay undefined; X-Plane resolves them when it loads the plugin.

# --- OpenGL (provided by the system / GPU driver) ---------------------------------------
if(SC_PLATFORM_DIR STREQUAL "lin_x64")
    set(OpenGL_GL_PREFERENCE GLVND)
endif()
find_package(OpenGL REQUIRED)                  # Ubuntu/Debian: apt install libgl-dev

if(MSVC)
    add_compile_options(/utf-8 /MP)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()
if(APPLE)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

# --- Dear ImGui (static) ------------------------------------------------------------
add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp")
target_include_directories(imgui SYSTEM PUBLIC "${IMGUI_DIR}" "${IMGUI_DIR}/misc/cpp")

# --- ImgWindow (static) -----------------------------------------------------------
add_library(imgwindow STATIC "${IMGWINDOW_DIR}/ImgWindow.cpp" "${IMGWINDOW_DIR}/ImgFontAtlas.cpp")
target_include_directories(imgwindow SYSTEM PUBLIC "${IMGWINDOW_DIR}")
target_compile_definitions(imgwindow PUBLIC IMGWINDOW_USE_PANEL_GRAPHICS)
target_link_libraries(imgwindow PUBLIC imgui xpsdk OpenGL::GL)
if(APPLE)
    target_link_libraries(imgwindow PUBLIC "-framework CoreGraphics")
endif()
if(WIN32)
    # The SDK has GL/gl.h but no GL/glext.h, which ImgWindow includes: libs/GL supplies it.
    set(SC_GLEXT_DIR "${SC_LIBS_DIR}" CACHE PATH "Windows: folder that holds GL/glext.h")
    target_include_directories(imgwindow SYSTEM PUBLIC "${SC_GLEXT_DIR}" "${SC_GLEXT_DIR}/GL")
endif()


add_library(fonts INTERFACE)
target_include_directories(fonts SYSTEM INTERFACE "${FONTS_DIR}")

# --- Plugin -------------------------------------------------------------------------
add_library(${PROJECT_NAME} MODULE
    # The plugin frame: entry points, the main window, shared values
    src/shared_cockpit.cpp
    src/PluginWindow.cpp
    src/PluginWindow.h
    src/SharedInfo.h
    src/Globals.cpp
    src/Globals.h
    # Screens: one folder each
    src/home/HomePage.cpp
    src/home/HomePage.h
    src/analyse/AnalysePage.cpp
    src/analyse/AnalysePage.h
    src/analyse/AircraftFinder.cpp
    src/analyse/AircraftFinder.h
    src/connect/ConnectPage.cpp
    src/connect/ConnectPage.h
    src/connect/SessionSettingsPage.cpp
    src/connect/SessionSettingsPage.h
    src/flightdeck/FlightDeckWindow.cpp
    src/flightdeck/FlightDeckWindow.h
    # Shared by several screens (the rest of shared/ is in sc_core)
    src/shared/analysis/Scanner.cpp
    src/shared/analysis/Scanner.h
    src/shared/analysis/Session.cpp
    src/shared/analysis/Session.h
    src/shared/ui/Fonts.cpp
    src/shared/ui/Fonts.h
    src/shared/ui/Theme.h
    src/shared/ui/Widgets.cpp
    src/shared/ui/Widgets.h
    src/shared/ui/ShareCard.cpp
    src/shared/ui/ShareCard.h
    src/shared/xp/XPlane.cpp
    src/shared/xp/XPlane.h
    # Sharing the controls (uses X-Plane)
    src/shared/sync/CommandSync.cpp
    src/shared/sync/CommandSync.h
    src/shared/sync/DatarefSync.cpp
    src/shared/sync/DatarefSync.h
    src/shared/sync/FlightFollow.cpp
    src/shared/sync/FlightFollow.h
    src/shared/sync/FlightInputs.cpp
    src/shared/sync/FlightInputs.h
    src/shared/sync/FlightSync.cpp
    src/shared/sync/FlightSync.h
    src/shared/sync/FlightTrace.cpp
    src/shared/sync/FlightTrace.h
    src/shared/sync/PositionSync.cpp
    src/shared/sync/PositionSync.h
    src/shared/sync/LoadSync.cpp
    src/shared/sync/LoadSync.h
    src/shared/sync/WeatherSync.cpp
    src/shared/sync/WeatherSync.h)
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(MSVC)
    # /Zc:preprocessor: the standard preprocessor (the LOG_* macros use __VA_OPT__).
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /Zc:preprocessor /Zc:__cplusplus)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wshadow)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE sc_core imgwindow fonts)

if(SC_PLATFORM_DIR STREQUAL "lin_x64")
    # Static C++ runtime; export only the five X-Plane entry points.
    set(SC_EXPORTS "${CMAKE_CURRENT_SOURCE_DIR}/src/shared_cockpit.sym")
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS "${SC_EXPORTS}")
    target_link_options(${PROJECT_NAME} PRIVATE
        -static-libgcc -static-libstdc++ -Wl,--exclude-libs,ALL "-Wl,--version-script=${SC_EXPORTS}"
        -Wl,-O1 -Wl,--as-needed)
elseif(APPLE)
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,-dead_strip)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    SUFFIX ".xpl"
    OUTPUT_NAME "${PROJECT_NAME}"
    LIBRARY_OUTPUT_DIRECTORY "${SC_OUT_DIR}/${PROJECT_NAME}/${SC_PLATFORM_DIR}")
install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION "${PROJECT_NAME}/${SC_PLATFORM_DIR}")

# --- sc_manip_types.ini, shipped next to the plugin ----------------------------------------
set(SC_TYPES_INI "${CMAKE_CURRENT_SOURCE_DIR}/libs/sc_manip_types.ini")
set(_sc_ini_out "${SC_OUT_DIR}/${PROJECT_NAME}/sc_manip_types.ini")
add_custom_command(OUTPUT "${_sc_ini_out}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SC_TYPES_INI}" "${_sc_ini_out}"
    DEPENDS "${SC_TYPES_INI}"
    COMMENT "Copying sc_manip_types.ini next to the plugin"
    VERBATIM)
add_custom_target(${PROJECT_NAME}_ini ALL DEPENDS "${_sc_ini_out}")
add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_ini)
install(FILES "${SC_TYPES_INI}" DESTINATION "${PROJECT_NAME}")

# --- Deploy into local X-Plane installations (after every build) --------------------
# Copies the plugin and sc_manip_types.ini to <XP root>/Resources/plugins/shared_cockpit/
# for each root in SC_DEPLOY_XP_ROOTS that exists. -DSC_DEPLOY=OFF to disable.
option(SC_DEPLOY "Copy the plugin into local X-Plane installations after each build" ON)
# SC_DEPLOY_HOME: the folder that holds the X-Plane installations.
# Windows: C:/ by default (C:/xp12). Linux / macOS: your home folder.
if(WIN32)
    set(SC_DEPLOY_HOME "C:" CACHE PATH "Folder that holds the X-Plane installations (C: = C:/xp12)")
else()
    set(SC_DEPLOY_HOME "$ENV{HOME}" CACHE PATH "Folder that holds the X-Plane installations")
endif()
string(REGEX REPLACE "[/\\]+$" "" _sc_home "${SC_DEPLOY_HOME}")    # no trailing slash: avoids C://xp12

# Where to deploy (each folder that exists gets the plugin).
if(WIN32)
    set(SC_DEPLOY_XP_ROOTS
        "${_sc_home}/xp12"
        CACHE STRING "X-Plane root folders to deploy the plugin into (; separated)")
else()
    set(SC_DEPLOY_XP_ROOTS
        "${_sc_home}/xp12" "${_sc_home}/xp12d" "${_sc_home}/xp12b" "${_sc_home}/xp12steam"
        CACHE STRING "X-Plane root folders to deploy the plugin into (; separated)")
endif()

if(SC_DEPLOY)
    set(_sc_deploy_script "${CMAKE_BINARY_DIR}/deploy_plugin.cmake")
    file(WRITE "${_sc_deploy_script}" [=[
string(REPLACE "|" ";" XP_ROOTS "${XP_ROOTS}")
get_filename_component(_name "${PLUGIN_FILE}" NAME)
foreach(_root IN LISTS XP_ROOTS)
    set(_plugins "${_root}/Resources/plugins")
    if(IS_DIRECTORY "${_plugins}")
        set(_dest "${_plugins}/${PLUGIN_NAME}/${PLATFORM_DIR}")
        file(MAKE_DIRECTORY "${_dest}")
        configure_file("${PLUGIN_FILE}" "${_dest}/${_name}.tmp" COPYONLY)
        file(RENAME "${_dest}/${_name}.tmp" "${_dest}/${_name}")
        configure_file("${INI_FILE}" "${_plugins}/${PLUGIN_NAME}/sc_manip_types.ini" COPYONLY)
        message(STATUS "${PLUGIN_NAME}: deployed to ${_dest}")
    endif()
endforeach()
]=])
    string(REPLACE ";" "|" _sc_roots_arg "${SC_DEPLOY_XP_ROOTS}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DXP_ROOTS=${_sc_roots_arg}"
            "-DPLUGIN_FILE=$<TARGET_FILE:${PROJECT_NAME}>"
            "-DPLUGIN_NAME=${PROJECT_NAME}"
            "-DPLATFORM_DIR=${SC_PLATFORM_DIR}"
            "-DINI_FILE=${SC_TYPES_INI}"
            -P "${_sc_deploy_script}"
        COMMENT "Deploying ${PROJECT_NAME} to local X-Plane installations"
        VERBATIM)
endif()
