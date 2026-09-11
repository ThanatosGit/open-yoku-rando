cmake_minimum_required(VERSION 3.24)
project(open_yoku_rando VERSION 0.1.0 LANGUAGES CXX)

# The DLL reads and grows std::string and std::vector objects owned by Yoku.exe, so it must use the same
# compiler ABI and the same CRT heap: MSVC, x64, dynamic release CRT, no iterator debugging.
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Build with MSVC for x64 (see build.cmd).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _ITERATOR_DEBUG_LEVEL=0)
# The DLLs use whatever msvcp140.dll the game loaded, which on a player's machine can be any VC++ 2019 or later
# redistributable (Yoku.exe itself needs vcruntime140_1.dll, so nothing older). Since MSVC 14.40, std::mutex has a
# constexpr constructor that crashes with a msvcp140.dll older than 14.40; this keeps the old one.
add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
add_compile_options(/W4 /utf-8 /permissive- /Zi)
add_link_options(/DEBUG)

add_library(open_yoku_rando SHARED
    dll/entry.cpp
    dll/game.cpp
    dll/goal.cpp
    dll/hooks.cpp
    dll/item_rules.cpp
    dll/log.cpp
    dll/lua_bridge.cpp
    dll/messages.cpp
    dll/multiworld.cpp
    dll/new_game.cpp
    dll/overlay.cpp
    dll/pickup_text.cpp
    dll/progressive.cpp
    dll/remote_api.cpp
    dll/verify.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp
)
target_include_directories(open_yoku_rando PRIVATE common third_party/imgui third_party/imgui/backends)
target_compile_definitions(open_yoku_rando PRIVATE OYR_VERSION="${PROJECT_VERSION}")
target_compile_definitions(open_yoku_rando PRIVATE IMGUI_DISABLE_WIN32_FUNCTIONS IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS)
target_link_libraries(open_yoku_rando PRIVATE ws2_32 d3d11 dxgi delayimp)
target_link_options(open_yoku_rando PRIVATE /DELAYLOAD:d3d11.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:d3dcompiler_47.dll)

# Placed in the game folder as xinput9_1_0.dll next to open_yoku_rando.dll, it starts the DLL with the game however
# the game is started; see proxy/xinput_proxy.cpp.
add_library(xinput_proxy SHARED proxy/xinput_proxy.cpp)
set_target_properties(xinput_proxy PROPERTIES OUTPUT_NAME xinput9_1_0)

# The patcher installs both into the game folder, so every build copies them into its package data.
set(PACKAGE_NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src/open_yoku_rando/files/native")
foreach(target open_yoku_rando xinput_proxy)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_NATIVE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${PACKAGE_NATIVE_DIR}")
endforeach()
