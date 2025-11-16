# Teensy 4.1 Toolchain File
# This file configures CMake to cross-compile for Teensy 4.1 (ARM Cortex-M7)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)

# Teensy-specific settings
set(TEENSY_VERSION 41 CACHE STRING "Teensy version")
set(CPU_CORE_SPEED 600000000 CACHE STRING "CPU speed in Hz (600MHz default)")

# Compiler paths - Adjust these based on your Teensyduino installation
set(COMPILER_PATH "C:/Users/levon/AppData/Local/Arduino15/packages/teensy/tools/teensy-compile/11.3.1/arm/bin/")
set(COMPILERPATH "C:/Users/levon/AppData/Local/Arduino15/packages/teensy/tools/teensy-compile/11.3.1/arm/bin/")

# Alternatively, use environment variable if set:
# set(COMPILER_PATH $ENV{TEENSYDUINO_PATH}/hardware/tools/arm/bin/)

# Set compilers
set(CMAKE_C_COMPILER ${COMPILER_PATH}arm-none-eabi-gcc.exe)
set(CMAKE_CXX_COMPILER ${COMPILER_PATH}arm-none-eabi-g++.exe)
set(CMAKE_ASM_COMPILER ${COMPILER_PATH}arm-none-eabi-gcc.exe)
set(CMAKE_AR ${COMPILER_PATH}arm-none-eabi-ar.exe)
set(CMAKE_RANLIB ${COMPILER_PATH}arm-none-eabi-ranlib.exe)
set(CMAKE_OBJCOPY ${COMPILER_PATH}arm-none-eabi-objcopy.exe)
set(CMAKE_SIZE ${COMPILER_PATH}arm-none-eabi-size.exe)

# Don't run the linker on compiler check
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Search for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
