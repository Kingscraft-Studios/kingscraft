cmake_minimum_required(VERSION 3.31)

project(KCLink LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Collect source files
file(GLOB_RECURSE KCLINK_SOURCES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE KCLINK_INTERNAL_SOURCES CONFIGURE_DEPENDS src/internal/*.cpp)
list(APPEND KCLINK_SOURCES ${KCLINK_INTERNAL_SOURCES})
list(REMOVE_DUPLICATES KCLINK_SOURCES)

file(GLOB_RECURSE KCLINK_HEADERS CONFIGURE_DEPENDS include/*.hpp)
file(GLOB_RECURSE KCLINK_INTERNAL_HEADERS CONFIGURE_DEPENDS include/internal/*.hpp)

# Build shared library
add_library(KCLink SHARED
        ${KCLINK_SOURCES}
        ${KCLINK_HEADERS}
        ${KCLINK_INTERNAL_HEADERS}
)

# Public headers are the only headers consumers can include.
target_include_directories(KCLink
        PUBLIC include
        PRIVATE src
)

# Internal headers refuse to compile without this definition, and internal
# symbols are hidden from the shared library's export table.
target_compile_definitions(KCLink PRIVATE
        KCLINK_BUILD=1
        KCLINK_INTERNAL_COMPILATION=1
)

# Shared library properties
set_target_properties(KCLink PROPERTIES
        OUTPUT_NAME KCLink
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
)
