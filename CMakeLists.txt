cmake_minimum_required(VERSION 3.21)
project(laser-vault LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 REQUIRED CONFIG)

add_executable(laser-vault
    src/main.c
    src/video/framefile.c
    src/video/disc_video.c
)
target_include_directories(laser-vault PRIVATE src)
target_link_libraries(laser-vault PRIVATE SDL3::SDL3)

# FFmpeg: vcpkg ships a FindFFMPEG wrapper; elsewhere (Linux distro packages,
# Homebrew) pkg-config is the standard way to find it.
if(VCPKG_TOOLCHAIN)
    find_package(FFMPEG REQUIRED)
    target_include_directories(laser-vault SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_directories(laser-vault PRIVATE ${FFMPEG_LIBRARY_DIRS})
    target_link_libraries(laser-vault PRIVATE ${FFMPEG_LIBRARIES})
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)
    target_link_libraries(laser-vault PRIVATE PkgConfig::LIBAV)
endif()

if(MSVC)
    target_compile_options(laser-vault PRIVATE /W4)
    target_compile_definitions(laser-vault PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(laser-vault PRIVATE -Wall -Wextra)
endif()
