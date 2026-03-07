cmake_minimum_required(VERSION 3.10)
project(Auto-C-To-CUDA CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type
set(CMAKE_BUILD_TYPE Release)

# ROSE installation path (can be overridden with -DROSE_INSTALL=path)
set(ROSE_INSTALL "/usr/rose" CACHE PATH "ROSE installation directory")

# ROSE include and lib directories
set(ROSE_INCLUDE_DIR "${ROSE_INSTALL}/include/rose")
set(ROSE_LIB_DIR "${ROSE_INSTALL}/lib")

# Boost include directory and flags
set(BOOST_CPPFLAGS "-pthread -I/usr/include")

# Compiler flags
set(CMAKE_CXX_FLAGS "-g -O2 -Wall")

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ROSE_INCLUDE_DIR}
    /usr/include
)

# Add pthread support
find_package(Threads REQUIRED)

# Find ROSE library
find_library(ROSE_LIB rose HINTS ${ROSE_LIB_DIR})

if(NOT ROSE_LIB)
    message(FATAL_ERROR "ROSE library not found in ${ROSE_LIB_DIR}. Please set ROSE_INSTALL correctly.")
endif()

message(STATUS "ROSE installation: ${ROSE_INSTALL}")
message(STATUS "ROSE include dir: ${ROSE_INCLUDE_DIR}")
message(STATUS "ROSE library: ${ROSE_LIB}")

# Create output directory for object files
set(BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/build")
file(MAKE_DIRECTORY ${BUILD_DIR})

# Define library sources and headers
set(NORMALIZE_SRC
    include/normalize/normalize.cpp
    include/normalize/normalize.hpp
)

set(AFFINE_SRC
    include/affine/affine.cpp
    include/affine/affine.hpp
    include/loop_attr.hpp
)

set(DEPENDENCY_SRC
    include/dependency/dependency.cpp
    include/dependency/dependency.hpp
    include/loop_attr.hpp
)

set(PARALLEL_SRC
    include/parallel/parallel.cpp
    include/parallel/parallel.hpp
    include/loop_attr.hpp
)

set(KERNEL_SRC
    include/kernel/kernel.cpp
    include/kernel/kernel.hpp
    include/loop_attr.hpp
)

set(PREPROCESS_SRC
    include/preprocess/preprocess.cpp
    include/preprocess/preprocess.hpp
)

# Create object libraries for each module
add_library(normalize_obj OBJECT ${NORMALIZE_SRC})
set_target_properties(normalize_obj PROPERTIES
    CXX_FLAGS "${CMAKE_CXX_FLAGS}"
    POSITION_INDEPENDENT_CODE ON
)

add_library(affine_obj OBJECT ${AFFINE_SRC})
set_target_properties(affine_obj PROPERTIES
    CXX_FLAGS "${CMAKE_CXX_FLAGS}"
    POSITION_INDEPENDENT_CODE ON
)

add_library(dependency_obj OBJECT ${DEPENDENCY_SRC})
set_target_properties(dependency_obj PROPERTIES
    CXX_FLAGS "${CMAKE_CXX_FLAGS}"
    POSITION_INDEPENDENT_CODE ON
)

add_library(kernel_obj OBJECT ${KERNEL_SRC})
set_target_properties(kernel_obj PROPERTIES
    CXX_FLAGS "${CMAKE_CXX_FLAGS}"
    POSITION_INDEPENDENT_CODE ON
)

add_library(parallel_obj OBJECT ${PARALLEL_SRC})
set_target_properties(parallel_obj PROPERTIES
    CXX_FLAGS "${CMAKE_CXX_FLAGS}"
    POSITION_INDEPENDENT_CODE ON
)

add_library(preprocess_obj OBJECT ${PREPROCESS_SRC})
set_target_properties(preprocess_obj PROPERTIES
    CXX_FLAGS "${CMAKE_CXX_FLAGS}"
    POSITION_INDEPENDENT_CODE ON
)

# Create the main executable
add_executable(translate.out
    translate.cpp
    $<TARGET_OBJECTS:normalize_obj>
    $<TARGET_OBJECTS:affine_obj>
    $<TARGET_OBJECTS:dependency_obj>
    $<TARGET_OBJECTS:kernel_obj>
    $<TARGET_OBJECTS:parallel_obj>
    $<TARGET_OBJECTS:preprocess_obj>
)

# Link libraries
target_link_libraries(translate.out
    ${ROSE_LIB}
    Threads::Threads
)

# Set output directory
set_target_properties(translate.out PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Custom clean target (optional)
add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_BINARY_DIR}/build
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/translate.out
    COMMENT "Cleaning build artifacts"
)

# Print build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C++ flags: ${CMAKE_CXX_FLAGS}")