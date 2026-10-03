cmake_minimum_required(VERSION 3.24)
project(ZoneSmith VERSION 1.0.0 LANGUAGES CXX)
if(NOT WIN32)
    message(FATAL_ERROR "ZoneSmith supports Windows only")
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(ZONESMITH_BUILD_STANDALONE "Build the standalone tray app" ${PROJECT_IS_TOP_LEVEL})
add_library(ZoneSmithCore STATIC
    src/OverlayWindow.cpp
    src/Overlay.cpp
    src/Config.cpp
    src/AppState.cpp
    src/App.cpp
    src/WindowUtils.cpp
    src/Snap.cpp
    src/WindowCycle.cpp
    src/InputHooks.cpp
)
add_library(ZoneSmith::Core ALIAS ZoneSmithCore)
target_compile_features(ZoneSmithCore PUBLIC cxx_std_20)
target_include_directories(ZoneSmithCore PUBLIC include PRIVATE include/zonesmith)
target_compile_definitions(ZoneSmithCore PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(ZoneSmithCore PUBLIC user32 gdi32 shell32 dwmapi)
if(MSVC)
    target_compile_options(ZoneSmithCore PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(ZoneSmithCore PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(ZONESMITH_BUILD_STANDALONE)
    add_executable(ZoneSmith WIN32 src/main.cpp)
    target_link_libraries(ZoneSmith PRIVATE ZoneSmith::Core)
    if(MINGW)
        target_link_options(ZoneSmith PRIVATE -municode)
    endif()
endif()
