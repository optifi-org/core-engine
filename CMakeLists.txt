cmake_minimum_required(VERSION 3.10)
project(optifi-core-engine)

set(CMAKE_CXX_STANDARD 17)

# --- Source files ---
set(SOURCES
    common/src/main.cpp
    common/src/PacketProcessor.cpp
    common/src/Profiler.cpp
    common/src/UsbHardware.cpp
)

# --- Platform Detection ---
if(WIN32 OR MINGW OR MSVC)
    message(STATUS "[CONF] Target Platform: Windows")
    list(APPEND SOURCES platform/windows/src/PlatformWindows.cpp)
    add_definitions(-D_WIN32_WINNT=0x0600) # Target Windows Vista or higher
else()
    message(STATUS "[CONF] Target Platform: Linux")
    list(APPEND SOURCES platform/linux/src/PlatformLinux.cpp)
    
    # Thread support for Linux
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
endif()

# --- Dependency: libusb ---
# On Linux we use PkgConfig, on Windows we assume it's in the system path or MinGW
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
    include_directories(${LIBUSB_INCLUDE_DIRS})
    link_directories(${LIBUSB_LIBRARY_DIRS})
else()
    # On Windows/MinGW, libusb-1.0 is often provided by the environment
    # or we expect the user to have it installed.
    # We try to find it gracefully.
    find_path(LIBUSB_INCLUDE_DIR NAMES libusb-1.0/libusb.h libusb.h)
    if(LIBUSB_INCLUDE_DIR)
        include_directories(${LIBUSB_INCLUDE_DIR})
    endif()
endif()

# --- Include directories ---
include_directories(common/include)

add_executable(optifi-core-engine ${SOURCES})

# --- Linking ---
if(WIN32)
    # Link Windows-specific libraries
    target_link_libraries(optifi-core-engine usb-1.0 ws2_32)
else()
    target_link_libraries(optifi-core-engine ${LIBUSB_LIBRARIES} Threads::Threads)
endif()
