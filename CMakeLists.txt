cmake_minimum_required(VERSION 3.10)
project(optifi-core-engine)

set(CMAKE_CXX_STANDARD 17)

# Include directories
include_directories(common/include)

# Source files
set(SOURCES
    common/src/main.cpp
    common/src/PacketProcessor.cpp
    common/src/Profiler.cpp
)

if(WIN32)
    list(APPEND SOURCES platform/windows/src/PlatformWindows.cpp)
    # Add any Windows-specific libraries here
else()
    list(APPEND SOURCES platform/linux/src/PlatformLinux.cpp)
    # Add pthread for Linux threads
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
endif()

add_executable(optifi-core-engine ${SOURCES})

if(NOT WIN32)
    target_link_libraries(optifi-core-engine Threads::Threads)
endif()
