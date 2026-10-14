# Turn a file into C source, so the program can carry it inside itself.
#
# Run as a script: cmake -DINPUT=file -DOUTPUT=file.c -DNAME=symbol -P embed_file.cmake
# Produces:
#     const unsigned char symbol[] = {0x00, 0x01, ...};
#     const unsigned long symbol_size = ...;

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hex_length)
math(EXPR size "${hex_length} / 2")

# "a1b2c3..." -> "0xa1,0xb2,0xc3,...", then break into lines of 16 bytes.
# (CMake's regular expressions have no {16}, so repeat the pattern instead.)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REPEAT "0x[0-9a-f][0-9a-f]," 16 sixteen_bytes)
string(REGEX REPLACE "(${sixteen_bytes})" "\\1\n    " bytes "${bytes}")

get_filename_component(source_name "${INPUT}" NAME)
file(WRITE "${OUTPUT}"
    "// Generated from ${source_name} by cmake/embed_file.cmake. Do not edit.\n"
    "const unsigned char ${NAME}[] = {\n    ${bytes}\n};\n"
    "const unsigned long ${NAME}_size = ${size};\n")
