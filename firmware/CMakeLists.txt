cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME firmware)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Drivers/WAVESHARE_OLED/Config/DEV_Config.c
    Drivers/WAVESHARE_OLED/OLED/OLED_1in5.c
    Drivers/WAVESHARE_OLED/GUI/GUI_Paint.c
    Drivers/WAVESHARE_OLED/Fonts/font8.c
    Drivers/WAVESHARE_OLED/Fonts/font12.c
    Drivers/WAVESHARE_OLED/Fonts/font16.c
    Drivers/WAVESHARE_OLED/Fonts/font20.c
    Drivers/WAVESHARE_OLED/Fonts/font24.c
    Core/UI/circle_of_fifths.c
    Core/UI/menu.c
    Core/UI/text.c
    Core/UI/boot.c
    Core/UI/app.c
    USB_DEVICE/App/usbd_midi.c
    Core/IO/audio.c
    Core/IO/cv_out.c
    Core/IO/gate_out.c
    Core/IO/analog_in.c
    Core/IO/clock_in.c
    Core/IO/sd_diskio.c
    Core/IO/sd_fs.c
    Core/IO/wav.c
    Core/IO/korg_ms.c
    Core/IO/multisample.c
    Core/IO/wt_osc.c
    Core/DSP/adsr.c
    Core/DSP/svf.c
    Core/Music/arp.c
    Core/Src/encoder.c
    # FatFs (ChaN R0.15) — SD-card filesystem, LFN + exFAT
    Middlewares/Third_Party/FatFs/src/ff.c
    Middlewares/Third_Party/FatFs/src/ffunicode.c
    Middlewares/Third_Party/FatFs/src/ffsystem.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Drivers/WAVESHARE_OLED/Config
    Drivers/WAVESHARE_OLED/OLED
    Drivers/WAVESHARE_OLED/GUI
    Drivers/WAVESHARE_OLED/Fonts
    Core/UI
    Core/IO
    Core/DSP
    Core/Music
    Middlewares/Third_Party/FatFs/src
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# USB personality: default MIDI device; -DUSB_SERIAL_DEBUG=ON enumerates as a
# CDC serial console instead (printf over USB). See usb-midi.md §3.
option(USB_SERIAL_DEBUG "USB enumerates as a CDC serial console instead of the MIDI device" OFF)
if(USB_SERIAL_DEBUG)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE USB_SERIAL_DEBUG=1)
    message(STATUS "USB personality: CDC serial debug console")
else()
    message(STATUS "USB personality: MIDI device")
endif()

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
