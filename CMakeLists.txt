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
    src/ldp/pr8210.c
    src/ldp/vip9500sg.c
)
target_include_directories(lv_ldp PUBLIC src)
if(NOT WIN32)
    target_link_libraries(lv_ldp PUBLIC m)  # fmod
endif()

# Writing PNG files (for screenshots), with zlib.
add_library(lv_png STATIC
    src/util/png.c
)
target_include_directories(lv_png PUBLIC src)
target_link_libraries(lv_png PUBLIC ZLIB::ZLIB)

# The emulator.
# The window icons are built into the program (as a C array generated
# from the .ico file), so it never has to find the file at run time.
set(EMBEDDED_ICONS dlair astron laser)
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
    src/games/machine.c
    src/audio/disc_audio.c
    src/ui/bezel.c
    src/ui/icon.c
    ${EMBEDDED_ICON_SOURCES}
)
target_link_libraries(laser-vault PRIVATE lv_video lv_ldp lv_cine lv_universal lv_funai lv_stern lv_konami lv_sega lv_rdi lv_png SDL3::SDL3)

# Tool: turn a range of disc frames into an animated GIF.
add_executable(disc-gif
    src/tools/disc_gif.c
    src/tools/gif.c
)
target_link_libraries(disc-gif PRIVATE lv_video lv_png)

# The CPUs: Z80, 6809 and COP421.
add_library(lv_cpu STATIC
    src/cpu/z80.c
    src/cpu/m6809.c
    src/cpu/cop421.c
)
target_include_directories(lv_cpu PUBLIC src)

# Test: run the ZEXDOC/ZEXALL instruction exercisers on the Z80.
add_executable(z80-zex
    tests/z80_zex.c
)
target_link_libraries(z80-zex PRIVATE lv_cpu)

# Reading ROM sets, from zips or folders.
add_library(lv_roms STATIC
    src/util/roms.c
    src/util/zip.c
)
target_include_directories(lv_roms PUBLIC src)
target_link_libraries(lv_roms PUBLIC ZLIB::ZLIB)

# The sound chips (AY-3-8910, SN76496, SSI-263), and recorded sound effects.
add_library(lv_sound STATIC
    src/audio/ay8910.c
    src/audio/sn76496.c
    src/audio/samples.c
    src/audio/ssi263.c
)
target_include_directories(lv_sound PUBLIC src)
if(NOT WIN32)
    target_link_libraries(lv_sound PUBLIC m)  # powf
endif()

# The Cinematronics laserdisc board (CPU, memory map, I/O) that runs Dragon's
# Lair and Space Ace, and the autoplayer.
add_library(lv_cine STATIC
    src/games/cinematronics/board.c
    src/games/cinematronics/autoplay.c
)
target_link_libraries(lv_cine PUBLIC lv_cpu lv_ldp lv_roms lv_sound)

# Universal's System 1 laserdisc board, which runs Super Don Quix-ote.
add_library(lv_universal STATIC
    src/games/universal/board.c
)
target_link_libraries(lv_universal PUBLIC lv_cpu lv_ldp lv_roms lv_sound)

# Funai's laserdisc board, which runs Esh's Aurunmilla.
add_library(lv_funai STATIC
    src/games/funai/board.c
)
target_link_libraries(lv_funai PUBLIC lv_cpu lv_ldp lv_roms)

# Stern's laserdisc board, which runs Cliff Hanger, with its video chip.
add_library(lv_stern STATIC
    src/games/stern/board.c
    src/video/tms9928.c
)
target_link_libraries(lv_stern PUBLIC lv_cpu lv_ldp lv_roms)

# Konami's laserdisc board, which runs Badlands.
add_library(lv_konami STATIC
    src/games/konami/board.c
)
target_link_libraries(lv_konami PUBLIC lv_cpu lv_ldp lv_roms lv_sound)

# Sega's laserdisc board, which runs Astron Belt.
add_library(lv_sega STATIC
    src/games/sega/board.c
)
target_link_libraries(lv_sega PUBLIC lv_cpu lv_ldp lv_roms lv_sound)

# RDI's laserdisc board, which runs Thayer's Quest.
add_library(lv_rdi STATIC
    src/games/rdi/board.c
)
target_link_libraries(lv_rdi PUBLIC lv_cpu lv_ldp lv_roms lv_sound)

# Tool: run a game on that board headless and print what it does.
add_executable(cine-trace
    src/tools/cine_trace.c
)
target_link_libraries(cine-trace PRIVATE lv_cine)

foreach(target lv_video lv_ldp lv_cpu lv_roms lv_sound lv_cine lv_universal lv_funai lv_stern lv_konami lv_sega lv_rdi lv_png laser-vault disc-gif z80-zex cine-trace)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endforeach()
