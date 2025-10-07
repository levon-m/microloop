cmake_minimum_required(VERSION 3.16)

# Must set toolchain before project()
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/teensy41.cmake)

# Project definition
project(MicroLoop VERSION 0.1.0 LANGUAGES C CXX ASM)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Teensy 4.1 specific settings
set(TEENSY 41)
set(F_CPU 600000000)
set(F_BUS 150000000)
set(LAYOUT US_ENGLISH)

# Arduino/Teensy paths (use CMake path normalization for Windows)
file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/Arduino15" ARDUINO15_PATH)
set(TEENSY_ROOT "${ARDUINO15_PATH}/packages/teensy")
set(TEENSY_CORES "${TEENSY_ROOT}/hardware/avr/1.59.0/cores/teensy4")
set(TEENSY_LIBS "${TEENSY_ROOT}/hardware/avr/1.59.0/libraries")

# Compiler flags for Teensy 4.1
add_compile_options(
    -mcpu=cortex-m7
    -mfloat-abi=hard
    -mfpu=fpv5-d16
    -mthumb
    -D__IMXRT1062__
    -DTEENSYDUINO=159
    -DARDUINO=10607
    -DARDUINO_TEENSY41
    -DF_CPU=${F_CPU}
    -DUSB_SERIAL
    -DLAYOUT_${LAYOUT}
    -D_GNU_SOURCE
    -fno-exceptions
    -fno-rtti
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
    -Werror=return-type
    -O2
)

# Linker flags
set(LINKER_SCRIPT "${TEENSY_CORES}/imxrt1062_t41.ld")
add_link_options(
    -mcpu=cortex-m7
    -mfloat-abi=hard
    -mfpu=fpv5-d16
    -mthumb
    -T${LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
)

# Library paths
set(WIRE_LIB "${TEENSY_LIBS}/Wire")
set(MIDI_LIB "${TEENSY_LIBS}/MIDI/src")
set(AUDIO_LIB "${TEENSY_LIBS}/Audio")
set(THREADS_LIB "${TEENSY_LIBS}/TeensyThreads")

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
    ${TEENSY_CORES}
    ${WIRE_LIB}
    ${MIDI_LIB}
    ${AUDIO_LIB}
    ${THREADS_LIB}
    ${TEENSY_LIBS}/SPI
)

# Teensy core sources (minimal set for now)
file(GLOB TEENSY_C_SOURCES "${TEENSY_CORES}/*.c")
file(GLOB TEENSY_CXX_SOURCES "${TEENSY_CORES}/*.cpp")
file(GLOB TEENSY_ASM_SOURCES "${TEENSY_CORES}/*.S")

add_library(teensy_core STATIC
    ${TEENSY_C_SOURCES}
    ${TEENSY_CXX_SOURCES}
    ${TEENSY_ASM_SOURCES}
)

target_include_directories(teensy_core PUBLIC ${TEENSY_CORES})

# Wire library
add_library(wire STATIC
    ${WIRE_LIB}/Wire.cpp
    ${WIRE_LIB}/WireIMXRT.cpp
    ${WIRE_LIB}/WireKinetis.cpp
)
target_include_directories(wire PUBLIC ${WIRE_LIB})
target_link_libraries(wire teensy_core)

# TeensyThreads library
add_library(teensy_threads STATIC
    ${THREADS_LIB}/TeensyThreads.cpp
    ${THREADS_LIB}/TeensyThreads-asm.S
)
target_include_directories(teensy_threads PUBLIC ${THREADS_LIB})
target_link_libraries(teensy_threads teensy_core)

# Audio library (exclude SD/SerialFlash files we don't need)
file(GLOB AUDIO_SOURCES "${AUDIO_LIB}/*.cpp" "${AUDIO_LIB}/*.c" "${AUDIO_LIB}/*.S")
file(GLOB AUDIO_UTILITY_SOURCES "${AUDIO_LIB}/utility/*.cpp" "${AUDIO_LIB}/utility/*.c")
list(FILTER AUDIO_SOURCES EXCLUDE REGEX ".*play_sd.*\\.cpp$")
list(FILTER AUDIO_SOURCES EXCLUDE REGEX ".*play_serialflash.*\\.cpp$")
add_library(audio STATIC ${AUDIO_SOURCES} ${AUDIO_UTILITY_SOURCES})
target_include_directories(audio PUBLIC
    ${AUDIO_LIB}
    ${AUDIO_LIB}/utility
    ${TEENSY_LIBS}/SD/src
    ${TEENSY_LIBS}/SdFat/src
    ${TEENSY_LIBS}/SerialFlash
)
target_link_libraries(audio teensy_core)

# MIDI library (header-only)
add_library(midi INTERFACE)
target_include_directories(midi INTERFACE ${MIDI_LIB})

# Utils library (now has trace.cpp and timekeeper.cpp)
add_library(microloop_utils STATIC
    utils/trace.cpp
    utils/timekeeper.cpp
)
target_include_directories(microloop_utils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
)
target_link_libraries(microloop_utils teensy_core)

# Project libraries
add_library(sgtl5000 STATIC src/SGTL5000.cpp)
target_include_directories(sgtl5000 PUBLIC include)
target_link_libraries(sgtl5000 teensy_core wire)

add_library(midi_io STATIC src/midi_io.cpp)
target_include_directories(midi_io PUBLIC include)
target_link_libraries(midi_io teensy_core midi teensy_threads microloop_utils)

add_library(app_logic STATIC src/app_logic.cpp)
target_include_directories(app_logic PUBLIC include)
target_link_libraries(app_logic teensy_core teensy_threads midi_io microloop_utils)

# MAIN
add_executable(microloop.elf src/main.cpp)

# TESTING
#add_executable(microloop.elf tests/run_tests.cpp)


target_link_libraries(microloop.elf
    teensy_core
    audio
    wire
    midi
    teensy_threads
    sgtl5000
    midi_io
    app_logic
    microloop_utils
    m  # Math library
)

# Generate HEX file for Teensy Loader
add_custom_command(TARGET microloop.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex -R .eeprom $<TARGET_FILE:microloop.elf> microloop.hex
    COMMENT "Creating HEX file for Teensy Loader"
)

# Print size
add_custom_command(TARGET microloop.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:microloop.elf>
    COMMENT "Binary size:"
)

# Print configuration summary
message(STATUS "")
message(STATUS "MicroLoop Configuration:")
message(STATUS "  Teensy Version: 4.1")
message(STATUS "  CPU Speed: ${F_CPU} Hz")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Teensy Cores: ${TEENSY_CORES}")
message(STATUS "")
