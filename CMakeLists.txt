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
set(SEESAW_LIB "${TEENSY_LIBS}/Adafruit_seesaw_Library")
set(NEOPIXEL_LIB "${TEENSY_LIBS}/Adafruit_NeoPixel")
set(BUSIO_LIB "${TEENSY_LIBS}/Adafruit_BusIO")
set(SSD1306_LIB "${TEENSY_LIBS}/Adafruit_SSD1306-2.5.15")
set(GFX_LIB "${TEENSY_LIBS}/Adafruit-GFX-Library-1.12.3")

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
    ${TEENSY_CORES}
    ${WIRE_LIB}
    ${MIDI_LIB}
    ${AUDIO_LIB}
    ${THREADS_LIB}
    ${SEESAW_LIB}
    ${NEOPIXEL_LIB}
    ${BUSIO_LIB}
    ${SSD1306_LIB}
    ${GFX_LIB}
    ${TEENSY_LIBS}/SPI
    ${TEENSY_LIBS}/SD/src
    ${TEENSY_LIBS}/SdFat/src
    ${TEENSY_LIBS}/SerialFlash
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

# SPI library (required by SSD1306)
add_library(spi STATIC
    ${TEENSY_LIBS}/SPI/SPI.cpp
)
target_include_directories(spi PUBLIC ${TEENSY_LIBS}/SPI)
target_link_libraries(spi teensy_core)

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

# Adafruit BusIO library (for I2C/SPI abstraction, required by Seesaw)
# REQUIRED: Dependency of Adafruit Seesaw Library
if(NOT EXISTS "${BUSIO_LIB}/Adafruit_I2CDevice.cpp")
    message(FATAL_ERROR "Adafruit BusIO Library NOT found. Download from https://github.com/adafruit/Adafruit_BusIO")
endif()
add_library(busio STATIC
    "${BUSIO_LIB}/Adafruit_I2CDevice.cpp"
    "${BUSIO_LIB}/Adafruit_SPIDevice.cpp"
)
target_include_directories(busio PUBLIC ${BUSIO_LIB})
target_link_libraries(busio teensy_core wire)
message(STATUS "Adafruit BusIO Library found")

# Adafruit Seesaw library (for Neokey choke button)
# REQUIRED: Install via Arduino IDE Library Manager: "Adafruit seesaw Library"
file(GLOB SEESAW_SOURCES "${SEESAW_LIB}/*.cpp")
if(NOT SEESAW_SOURCES)
    message(FATAL_ERROR "Adafruit Seesaw Library NOT found. Install via Arduino IDE: Tools → Manage Libraries → Search 'Adafruit seesaw'")
endif()
add_library(seesaw STATIC ${SEESAW_SOURCES})
target_include_directories(seesaw PUBLIC ${SEESAW_LIB})
target_link_libraries(seesaw teensy_core wire busio)
message(STATUS "Adafruit Seesaw Library found")

# Adafruit NeoPixel library (for Neokey LEDs)
# REQUIRED: Install via Arduino IDE Library Manager: "Adafruit NeoPixel"
if(NOT EXISTS "${NEOPIXEL_LIB}/Adafruit_NeoPixel.cpp")
    message(FATAL_ERROR "Adafruit NeoPixel Library NOT found. Install via Arduino IDE: Tools → Manage Libraries → Search 'Adafruit NeoPixel'")
endif()
add_library(neopixel STATIC "${NEOPIXEL_LIB}/Adafruit_NeoPixel.cpp")
target_include_directories(neopixel PUBLIC ${NEOPIXEL_LIB})
target_link_libraries(neopixel teensy_core)
message(STATUS "Adafruit NeoPixel Library found")

# Adafruit GFX library (for graphics primitives, required by SSD1306)
# REQUIRED: Install via Arduino IDE Library Manager: "Adafruit GFX Library"
if(NOT EXISTS "${GFX_LIB}/Adafruit_GFX.cpp")
    message(FATAL_ERROR "Adafruit GFX Library NOT found. Install via Arduino IDE: Tools → Manage Libraries → Search 'Adafruit GFX'")
endif()
add_library(gfx STATIC
    "${GFX_LIB}/Adafruit_GFX.cpp"
    "${GFX_LIB}/glcdfont.c"
)
target_include_directories(gfx PUBLIC ${GFX_LIB})
target_link_libraries(gfx teensy_core)
message(STATUS "Adafruit GFX Library found")

# Adafruit SSD1306 library (for OLED display)
# REQUIRED: Install via Arduino IDE Library Manager: "Adafruit SSD1306"
if(NOT EXISTS "${SSD1306_LIB}/Adafruit_SSD1306.cpp")
    message(FATAL_ERROR "Adafruit SSD1306 Library NOT found. Install via Arduino IDE: Tools → Manage Libraries → Search 'Adafruit SSD1306'")
endif()
add_library(ssd1306 STATIC "${SSD1306_LIB}/Adafruit_SSD1306.cpp")
target_include_directories(ssd1306 PUBLIC ${SSD1306_LIB})
target_link_libraries(ssd1306 teensy_core wire spi gfx)
message(STATUS "Adafruit SSD1306 Library found")

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

add_library(choke_io STATIC src/choke_io.cpp)
target_include_directories(choke_io PUBLIC include)
target_link_libraries(choke_io teensy_core teensy_threads seesaw neopixel busio microloop_utils)

add_library(display_io STATIC src/display_io.cpp)
target_include_directories(display_io PUBLIC include)
target_link_libraries(display_io teensy_core teensy_threads ssd1306 gfx wire microloop_utils)

add_library(effect_manager STATIC src/effect_manager.cpp)
target_include_directories(effect_manager PUBLIC include)
target_link_libraries(effect_manager teensy_core audio)

add_library(app_logic STATIC src/app_logic.cpp)
target_include_directories(app_logic PUBLIC include)
target_link_libraries(app_logic teensy_core teensy_threads midi_io choke_io display_io effect_manager microloop_utils)

# MAIN
add_executable(microloop.elf src/main.cpp)

# TESTING
#add_executable(microloop.elf tests/run_tests.cpp)

target_link_libraries(microloop.elf
    teensy_core
    audio
    wire
    spi
    midi
    teensy_threads
    sgtl5000
    midi_io
    choke_io
    display_io
    app_logic
    effect_manager
    seesaw
    neopixel
    busio
    ssd1306
    gfx
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
