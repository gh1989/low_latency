cmake_minimum_required(VERSION 3.25)
project(market_data_pipeline VERSION 0.1.0 LANGUAGES CXX)

# Require C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -march=native")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
elseif(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /O2 /arch:AVX2")
endif()

# Main library
add_library(core INTERFACE)
target_include_directories(core INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Thread library
find_package(Threads REQUIRED)

# Create directories if they don't exist
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/tests)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/benchmark)

# Ring buffer test executable
add_executable(ring_buffer_test src/tests/ring_buffer.cpp)
target_link_libraries(ring_buffer_test PRIVATE core Threads::Threads)

# Benchmark executable (will add later)
# add_executable(ring_buffer_benchmark src/benchmark/ring_buffer_benchmark.cpp)
# target_link_libraries(ring_buffer_benchmark PRIVATE core Threads::Threads)

# Main executable (will add later)
# add_executable(market_data_pipeline src/main.cpp)
# target_link_libraries(market_data_pipeline PRIVATE core Threads::Threads)

# Print configuration summary
message(STATUS "Configuration:")
message(STATUS "  C++ Standard: C++23")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
