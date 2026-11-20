cmake_minimum_required(VERSION 3.31)

project(KCLink LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Collect source files
file(GLOB_RECURSE KCLINK_SOURCES CONFIGURE_DEPENDS src/*.cpp)

file(GLOB_RECURSE KCLINK_HEADERS CONFIGURE_DEPENDS include/*.hpp)

# Build shared library
add_library(KCLink SHARED
        ${KCLINK_SOURCES}
        ${KCLINK_HEADERS}
)

# Public headers
target_include_directories(KCLink PUBLIC
        include
)

# Shared library properties
set_target_properties(KCLink PROPERTIES OUTPUT_NAME KCLink POSITION_INDEPENDENT_CODE ON)
