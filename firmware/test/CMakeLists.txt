cmake_minimum_required(VERSION 3.22)

# Host-side unit tests for the firmware's pure-logic modules. This is a SEPARATE
# project from the firmware build — it uses the native host compiler, not the
# arm-none-eabi cross toolchain. Modules under test are compiled directly from
# Core/; hardware dependencies are replaced by stubs under support/.
project(tachyon_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

set(FW ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Unity test framework (vendored, ThrowTheSwitch/Unity v2.6.0).
add_library(unity STATIC unity/unity.c)
target_include_directories(unity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/unity)

# arp.c — fully pure, compiled straight in.
add_executable(test_arp test_arp.c ${FW}/Core/Music/arp.c)
target_include_directories(test_arp PRIVATE ${FW}/Core/Music)

# wav.c — parses an open file; the FatFs surface (f_lseek/f_read/f_tell) is
# backed by an in-memory stub. support/ supplies the fake ff.h, so the real
# FatFs headers are intentionally NOT on the include path here.
add_executable(test_wav test_wav.c ${FW}/Core/IO/wav.c support/ff_fake.c)
target_include_directories(test_wav PRIVATE ${FW}/Core/IO ${CMAKE_CURRENT_SOURCE_DIR}/support)

foreach(t test_arp test_wav)
    target_link_libraries(${t} PRIVATE unity)
    target_compile_options(${t} PRIVATE -Wall -Wextra)
    add_test(NAME ${t} COMMAND ${t})
endforeach()
