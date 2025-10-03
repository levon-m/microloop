cmake_minimum_required(VERSION 3.16)

# Project definition
project(MicroLoop VERSION 0.1.0 LANGUAGES C CXX ASM)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Teensy configuration
set(TEENSY_VERSION 41 CACHE STRING "Teensy board version")
set(CPU_CORE_SPEED 600000000 CACHE STRING "CPU speed in Hz")

# Import teensy-cmake-macros
include(FetchContent)
FetchContent_Declare(
    teensy_cmake_macros
    GIT_REPOSITORY https://github.com/newdigate/teensy-cmake-macros
    GIT_TAG main
)
FetchContent_MakeAvailable(teensy_cmake_macros)
include(${teensy_cmake_macros_SOURCE_DIR}/CMakeLists.include.txt)

# Import required Arduino libraries
import_arduino_library(cores ${teensy_cores_SOURCE_DIR}/teensy4 avr util)
import_arduino_library_git(MIDI https://github.com/FortySevenEffects/arduino_midi_library.git master "")
import_arduino_library_git(Audio https://github.com/PaulStoffregen/Audio.git master "")
import_arduino_library_git(TeensyThreads https://github.com/ftrias/TeensyThreads.git master "")
import_arduino_library_git(Wire https://github.com/PaulStoffregen/Wire.git master "")

# Compile options for real-time safety and performance
add_compile_options(
    -fno-exceptions          # No exceptions (not real-time safe)
    -fno-rtti               # No RTTI (runtime type info) - saves space and determinism
    -ffunction-sections     # Each function in own section (better dead code elimination)
    -fdata-sections         # Each data in own section
    -Wall                   # All warnings
    -Wextra                 # Extra warnings
    -Werror=return-type     # Error on missing return
)

# Link options
add_link_options(
    -Wl,--gc-sections       # Garbage collect unused sections (reduces binary size)
)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
)

# Utils library (generic, reusable, testable components)
teensy_add_library(microloop_utils
    utils/spsc_queue.h
    utils/span.h
)
target_include_directories(microloop_utils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
)

# SGTL5000 codec library
teensy_add_library(sgtl5000
    src/SGTL5000.cpp
    include/SGTL5000.h
)
target_include_directories(sgtl5000 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
teensy_target_link_libraries(sgtl5000 cores Wire)

# MIDI I/O library
teensy_add_library(midi_io
    src/midi_io.cpp
    include/midi_io.h
)
target_include_directories(midi_io PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
teensy_target_link_libraries(midi_io cores MIDI TeensyThreads microloop_utils)

# Application logic library
teensy_add_library(app_logic
    src/app_logic.cpp
    include/app_logic.h
)
target_include_directories(app_logic PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
teensy_target_link_libraries(app_logic cores TeensyThreads midi_io microloop_utils)

# Main firmware executable
teensy_add_executable(microloop
    src/main.cpp
)
teensy_target_link_libraries(microloop
    cores
    Audio
    Wire
    MIDI
    TeensyThreads
    sgtl5000
    midi_io
    app_logic
    microloop_utils
)

# Print configuration summary
message(STATUS "")
message(STATUS "MicroLoop Configuration:")
message(STATUS "  Teensy Version: ${TEENSY_VERSION}")
message(STATUS "  CPU Speed: ${CPU_CORE_SPEED} Hz")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "")
