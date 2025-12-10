# Teensy 4.1 Toolchain File (Arduino-free, uses arm-none-eabi-* from PATH)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)

set(TEENSY_VERSION 41 CACHE STRING "Teensy version")
set(CPU_CORE_SPEED 600000000 CACHE STRING "CPU speed in Hz (600MHz default)")

# Find ARM GCC toolchain on PATH
find_program(ARM_GCC    arm-none-eabi-gcc)
find_program(ARM_GPP    arm-none-eabi-g++)
find_program(ARM_AR     arm-none-eabi-ar)
find_program(ARM_RANLIB arm-none-eabi-ranlib)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)
find_program(ARM_SIZE    arm-none-eabi-size)

if(NOT ARM_GCC OR NOT ARM_GPP OR NOT ARM_AR OR NOT ARM_RANLIB OR NOT ARM_OBJCOPY OR NOT ARM_SIZE)
    message(FATAL_ERROR
        "arm-none-eabi toolchain not found. Install GCC for ARM Embedded and "
        "ensure arm-none-eabi-* are in your PATH.")
endif()

set(CMAKE_C_COMPILER   ${ARM_GCC})
set(CMAKE_CXX_COMPILER ${ARM_GPP})
set(CMAKE_ASM_COMPILER ${ARM_GCC})

set(CMAKE_AR      ${ARM_AR})
set(CMAKE_RANLIB  ${ARM_RANLIB})
set(CMAKE_OBJCOPY ${ARM_OBJCOPY})
set(CMAKE_SIZE    ${ARM_SIZE})

# Don't try to link when CMake is just testing the compiler
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
