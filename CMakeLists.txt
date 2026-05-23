cmake_minimum_required(VERSION 3.10)
project(optifi-core-engine)

set(CMAKE_CXX_STANDARD 17)

# --- Robust Platform Detection ---
if(WIN32 OR MINGW OR MSVC OR CMAKE_SYSTEM_NAME STREQUAL "Windows" OR MSYS)
    set(OPTIFI_WINDOWS TRUE)
    message(STATUS "[CONF] Target Platform: Windows")
else()
    set(OPTIFI_WINDOWS FALSE)
    message(STATUS "[CONF] Target Platform: Linux")
endif()

# --- Source files ---
set(SOURCES
    common/src/main.cpp
    common/src/PacketProcessor.cpp
    common/src/Profiler.cpp
    common/src/UsbHardware.cpp
)

if(OPTIFI_WINDOWS)
    list(APPEND SOURCES platform/windows/src/PlatformWindows.cpp)
    add_definitions(-D_WIN32_WINNT=0x0600)
else()
    list(APPEND SOURCES platform/linux/src/PlatformLinux.cpp)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
endif()

# --- Dependency: libusb ---
if(NOT OPTIFI_WINDOWS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
    include_directories(${LIBUSB_INCLUDE_DIRS})
else()
    # On Windows/MSYS2, pkg-config might still be available
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(LIBUSB libusb-1.0)
    endif()
    
    if(LIBUSB_FOUND)
        include_directories(${LIBUSB_INCLUDE_DIRS})
    else()
        find_path(LIBUSB_INCLUDE_DIR NAMES libusb-1.0/libusb.h libusb.h)
        if(LIBUSB_INCLUDE_DIR)
            include_directories(${LIBUSB_INCLUDE_DIR})
        endif()
    endif()
endif()

# --- Include directories ---
include_directories(common/include)

add_executable(optifi-core-engine ${SOURCES})

# --- Linking (Matching Detection) ---
if(OPTIFI_WINDOWS)
    # Link Windows-specific libraries
    # In MSYS2, libusb-1.0 is the standard name
    target_link_libraries(optifi-core-engine usb-1.0 ws2_32)
else()
    target_link_libraries(optifi-core-engine ${LIBUSB_LIBRARIES} Threads::Threads)
endif()
