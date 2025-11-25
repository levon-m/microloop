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
set(MCP23017_LIB "${TEENSY_LIBS}/Adafruit-MCP23017-Arduino-Library-2.3.2/src")

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal
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
    ${MCP23017_LIB}
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

# SD library (for preset storage)
set(SD_LIB "${TEENSY_LIBS}/SD/src")
set(SDFAT_LIB "${TEENSY_LIBS}/SdFat/src")
file(GLOB SD_SOURCES "${SD_LIB}/*.cpp")
file(GLOB SDFAT_SOURCES "${SDFAT_LIB}/*.cpp" "${SDFAT_LIB}/*/*.cpp")
add_library(sd_card STATIC ${SD_SOURCES} ${SDFAT_SOURCES})
target_include_directories(sd_card PUBLIC ${SD_LIB} ${SDFAT_LIB})
target_link_libraries(sd_card teensy_core spi)
message(STATUS "SD library found")

# MIDI library (header-only)
add_library(midi INTERFACE)
target_include_directories(midi INTERFACE ${MIDI_LIB})

# Adafruit BusIO library (for I2C/SPI abstraction, required by Seesaw and MCP23017)
# REQUIRED: Dependency of Adafruit Seesaw Library and MCP23017 Library
if(NOT EXISTS "${BUSIO_LIB}/Adafruit_I2CDevice.cpp")
    message(FATAL_ERROR "Adafruit BusIO Library NOT found. Download from https://github.com/adafruit/Adafruit_BusIO")
endif()
add_library(busio STATIC
    "${BUSIO_LIB}/Adafruit_I2CDevice.cpp"
    "${BUSIO_LIB}/Adafruit_SPIDevice.cpp"
    "${BUSIO_LIB}/Adafruit_BusIO_Register.cpp"
    "${BUSIO_LIB}/Adafruit_GenericDevice.cpp"
)
target_include_directories(busio PUBLIC ${BUSIO_LIB})
target_link_libraries(busio teensy_core wire spi)
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

# Adafruit MCP23017 library (for encoder I/O expander)
# REQUIRED: Install via Arduino IDE Library Manager: "Adafruit MCP23017 Arduino Library"
if(NOT EXISTS "${MCP23017_LIB}/Adafruit_MCP23X17.cpp")
    message(FATAL_ERROR "Adafruit MCP23017 Library NOT found. Install via Arduino IDE: Tools → Manage Libraries → Search 'Adafruit MCP23017'")
endif()
add_library(mcp23017 STATIC
    "${MCP23017_LIB}/Adafruit_MCP23X17.cpp"
    "${MCP23017_LIB}/Adafruit_MCP23XXX.cpp"
)
target_include_directories(mcp23017 PUBLIC ${MCP23017_LIB})
target_link_libraries(mcp23017 teensy_core wire busio)
message(STATUS "Adafruit MCP23017 Library found")

# Utils library (now has trace.cpp and timekeeper.cpp)
add_library(microloop_utils STATIC
    src/core/Trace.cpp
    src/core/Timebase.cpp
)
target_include_directories(microloop_utils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)
target_link_libraries(microloop_utils teensy_core)

# Project libraries
# Note: Custom SGTL5000 driver backed up as SGTL5000_CUSTOM_BACKUP.cpp/.h
# Using Teensy Audio Library's AudioControlSGTL5000 instead

# HAL libraries (Hardware Abstraction Layer)
add_library(midi_io STATIC src/hal/MidiInput.cpp)
target_include_directories(midi_io PUBLIC src/hal src/core)
target_link_libraries(midi_io teensy_core midi teensy_threads microloop_utils)

add_library(neokey_io STATIC src/hal/NeokeyInput.cpp)
target_include_directories(neokey_io PUBLIC src/hal src/core)
target_link_libraries(neokey_io teensy_core teensy_threads seesaw neopixel busio microloop_utils)

add_library(oled_io STATIC src/hal/Ssd1306Display.cpp)
target_include_directories(oled_io PUBLIC src/hal src/core src/app)
target_link_libraries(oled_io teensy_core teensy_threads ssd1306 gfx wire microloop_utils)

add_library(mcp_io STATIC src/hal/Mcp23017Input.cpp)
target_include_directories(mcp_io PUBLIC src/hal src/core)
target_link_libraries(mcp_io teensy_core teensy_threads wire mcp23017 busio)

add_library(sd_io STATIC src/hal/SdCardStorage.cpp)
target_include_directories(sd_io PUBLIC src/hal src/core)
target_link_libraries(sd_io teensy_core sd_card)

# DSP libraries (Audio Effects)
add_library(effect_manager STATIC src/dsp/EffectManager.cpp)
target_include_directories(effect_manager PUBLIC src/dsp)
target_link_libraries(effect_manager teensy_core audio)

add_library(effect_quantization STATIC src/dsp/EffectQuantization.cpp)
target_include_directories(effect_quantization PUBLIC src/dsp src/core)
target_link_libraries(effect_quantization teensy_core microloop_utils)

add_library(audio_choke STATIC src/dsp/ChokeAudio.cpp)
target_include_directories(audio_choke PUBLIC src/dsp src/core)
target_link_libraries(audio_choke teensy_core audio microloop_utils)

add_library(audio_freeze STATIC src/dsp/FreezeAudio.cpp)
target_include_directories(audio_freeze PUBLIC src/dsp src/core)
target_link_libraries(audio_freeze teensy_core audio microloop_utils)

add_library(audio_stutter STATIC src/dsp/StutterAudio.cpp)
target_include_directories(audio_stutter PUBLIC src/dsp src/core)
target_link_libraries(audio_stutter teensy_core audio microloop_utils)

# App libraries (Application Logic)
add_library(encoder_handler STATIC src/app/EncoderHandler.cpp)
target_include_directories(encoder_handler PUBLIC src/app src/hal)
target_link_libraries(encoder_handler teensy_core mcp_io)

add_library(display_manager STATIC src/app/DisplayManager.cpp)
target_include_directories(display_manager PUBLIC src/app src/hal src/dsp)
target_link_libraries(display_manager teensy_core oled_io effect_manager)

add_library(choke_controller STATIC src/app/ChokeController.cpp)
target_include_directories(choke_controller PUBLIC src/app src/dsp src/hal src/core)
target_link_libraries(choke_controller teensy_core audio_choke effect_quantization display_manager neokey_io microloop_utils)

add_library(freeze_controller STATIC src/app/FreezeController.cpp)
target_include_directories(freeze_controller PUBLIC src/app src/dsp src/hal src/core)
target_link_libraries(freeze_controller teensy_core audio_freeze effect_quantization display_manager neokey_io microloop_utils)

add_library(stutter_controller STATIC src/app/StutterController.cpp)
target_include_directories(stutter_controller PUBLIC src/app src/dsp src/hal src/core)
target_link_libraries(stutter_controller teensy_core audio_stutter effect_quantization display_manager neokey_io microloop_utils)

add_library(global_controller STATIC src/app/GlobalController.cpp)
target_include_directories(global_controller PUBLIC src/app src/dsp src/hal)
target_link_libraries(global_controller teensy_core effect_quantization display_manager)

add_library(preset_controller STATIC src/app/PresetController.cpp)
target_include_directories(preset_controller PUBLIC src/app src/dsp src/hal src/core)
target_link_libraries(preset_controller teensy_core audio_stutter sd_io oled_io microloop_utils)

add_library(app_logic STATIC src/app/App.cpp)
target_include_directories(app_logic PUBLIC src/app src/dsp src/hal src/core)
target_link_libraries(app_logic
    teensy_core
    teensy_threads
    midi_io
    neokey_io
    oled_io
    mcp_io
    sd_io
    effect_manager
    microloop_utils
    effect_quantization
    encoder_handler
    display_manager
    choke_controller
    freeze_controller
    stutter_controller
    global_controller
    preset_controller
)

# MAIN
add_executable(microloop.elf src/main.cpp)

# ENCODER TEST
#add_executable(microloop.elf tests/test_encoders_main.cpp)

# UNIT TESTING
#add_executable(microloop.elf tests/run_tests.cpp)

target_link_libraries(microloop.elf
    teensy_core
    audio
    wire
    spi
    midi
    teensy_threads
    midi_io
    neokey_io
    oled_io
    mcp_io
    sd_io
    sd_card
    app_logic
    effect_manager
    effect_quantization
    audio_choke
    audio_freeze
    audio_stutter
    encoder_handler
    display_manager
    choke_controller
    freeze_controller
    stutter_controller
    global_controller
    preset_controller
    seesaw
    neopixel
    busio
    ssd1306
    gfx
    mcp23017
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
