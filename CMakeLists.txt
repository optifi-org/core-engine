cmake_minimum_required(VERSION 3.10)
project(optifi-core-engine)

set(CMAKE_CXX_STANDARD 17)

# Find dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

# Include directories
include_directories(common/include)
include_directories(${LIBUSB_INCLUDE_DIRS})

# Source files
set(SOURCES
    common/src/main.cpp
    common/src/PacketProcessor.cpp
    common/src/Profiler.cpp
    common/src/UsbHardware.cpp
)

if(WIN32)
    list(APPEND SOURCES platform/windows/src/PlatformWindows.cpp)
else()
    list(APPEND SOURCES platform/linux/src/PlatformLinux.cpp)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
endif()

add_executable(optifi-core-engine ${SOURCES})

target_link_libraries(optifi-core-engine ${LIBUSB_LIBRARIES})

if(NOT WIN32)
    target_link_libraries(optifi-core-engine Threads::Threads)
endif()
