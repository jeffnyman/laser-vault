cmake_minimum_required(VERSION 3.21)
project(laser-vault LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 REQUIRED CONFIG)

# Laserdisc video (framefile + MPEG-2 decoding), shared by the emulator and
# the tools.
add_library(lv_video STATIC
    src/video/framefile.c
    src/video/disc_video.c
    src/video/decomb.c
)
target_include_directories(lv_video PUBLIC src)

# FFmpeg: vcpkg ships a FindFFMPEG wrapper; elsewhere (Linux distro packages,
# Homebrew) pkg-config is the standard way to find it.
if(VCPKG_TOOLCHAIN)
    find_package(FFMPEG REQUIRED)
    target_include_directories(lv_video SYSTEM PUBLIC ${FFMPEG_INCLUDE_DIRS})
    target_link_directories(lv_video PUBLIC ${FFMPEG_LIBRARY_DIRS})
    target_link_libraries(lv_video PUBLIC ${FFMPEG_LIBRARIES})
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)
    target_link_libraries(lv_video PUBLIC PkgConfig::LIBAV)
endif()

# The emulator.
add_executable(laser-vault
    src/main.c
    src/ldp/ldp.c
)
target_link_libraries(laser-vault PRIVATE lv_video SDL3::SDL3)

# Tool: turn a range of disc frames into an animated GIF.
add_executable(disc-gif
    src/tools/disc_gif.c
    src/tools/gif.c
)
target_link_libraries(disc-gif PRIVATE lv_video)

foreach(target lv_video laser-vault disc-gif)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endforeach()
