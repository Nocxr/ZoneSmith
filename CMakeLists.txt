cmake_minimum_required(VERSION 3.20)

project(ZoneSmith LANGUAGES CXX)

add_executable(ZoneSmith WIN32 src/main.cpp)
target_compile_features(ZoneSmith PRIVATE cxx_std_20)
target_compile_definitions(ZoneSmith PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(ZoneSmith PRIVATE dwmapi user32 gdi32 shell32)

if(MSVC)
    target_compile_options(ZoneSmith PRIVATE /W4 /permissive- /EHsc)
endif()
