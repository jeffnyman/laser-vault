cmake_minimum_required(VERSION 3.21)
project(laser-vault LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 REQUIRED CONFIG)
find_package(ZLIB REQUIRED)

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

# The laserdisc player: the basic model plus the LD-V1000 protocol.
add_library(lv_ldp STATIC
    src/ldp/ldp.c
    src/ldp/ldv1000.c
)
target_include_directories(lv_ldp PUBLIC src)
if(NOT WIN32)
    target_link_libraries(lv_ldp PUBLIC m)  # fmod
endif()

# The emulator.
# Each game's window icon is built into the program (as a C array generated
# from the .ico file), so it never has to find the file at run time.
set(EMBEDDED_ICONS dlair spaceace)
set(EMBEDDED_ICON_SOURCES "")
foreach(icon ${EMBEDDED_ICONS})
    set(input ${CMAKE_SOURCE_DIR}/icons/${icon}.ico)
    set(output ${CMAKE_BINARY_DIR}/generated/icon_${icon}.c)
    add_custom_command(
        OUTPUT ${output}
        COMMAND ${CMAKE_COMMAND} -DINPUT=${input} -DOUTPUT=${output} -DNAME=icon_${icon}
                -P ${CMAKE_SOURCE_DIR}/cmake/embed_file.cmake
        DEPENDS ${input} ${CMAKE_SOURCE_DIR}/cmake/embed_file.cmake
        COMMENT "Embedding ${icon}.ico")
    list(APPEND EMBEDDED_ICON_SOURCES ${output})
endforeach()

add_executable(laser-vault
    src/main.c
    src/audio/disc_audio.c
    src/ui/bezel.c
    src/ui/icon.c
    ${EMBEDDED_ICON_SOURCES}
)
target_link_libraries(laser-vault PRIVATE lv_video lv_ldp lv_cine SDL3::SDL3)

# Tool: turn a range of disc frames into an animated GIF.
add_executable(disc-gif
    src/tools/disc_gif.c
    src/tools/gif.c
)
target_link_libraries(disc-gif PRIVATE lv_video)

# The Z80 CPU.
add_library(lv_cpu STATIC
    src/cpu/z80.c
)
target_include_directories(lv_cpu PUBLIC src)

# Test: run the ZEXDOC/ZEXALL instruction exercisers on the Z80.
add_executable(z80-zex
    tests/z80_zex.c
)
target_link_libraries(z80-zex PRIVATE lv_cpu)

# The Cinematronics laserdisc board (CPU, memory map, I/O) that runs Dragon's
# Lair and Space Ace, plus loading ROMs from zips, and the autoplayer.
add_library(lv_cine STATIC
    src/games/cinematronics/board.c
    src/games/cinematronics/autoplay.c
    src/audio/ay8910.c
    src/util/zip.c
)
target_link_libraries(lv_cine PUBLIC lv_cpu lv_ldp ZLIB::ZLIB)

# Tool: run a game on that board headless and print what it does.
add_executable(cine-trace
    src/tools/cine_trace.c
)
target_link_libraries(cine-trace PRIVATE lv_cine)

foreach(target lv_video lv_ldp lv_cpu lv_cine laser-vault disc-gif z80-zex cine-trace)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endforeach()
