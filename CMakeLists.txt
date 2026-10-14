cmake_minimum_required(VERSION 3.31)
project(kingscraft)

set(CMAKE_CXX_STANDARD 20)
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s ${CMAKE_EXE_LINKER_FLAGS}")

add_definitions(-DGLFW_STATIC)

# Project Include
include_directories(include)
include_directories(libs/glfw/include)
include_directories(libs/glm)
include_directories(libs/vulkan/include)
include_directories(libs/stb)
include_directories(libs/FastNoiseLite)
include_directories(libs/json)

add_subdirectory(libs/glfw)

# Vendored libshaderc (GLSL -> SPIR-V), built as a shared library
set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_COPYRIGHT_CHECK ON CACHE BOOL "" FORCE)
set(SHADERC_ENABLE_WERROR_COMPILE OFF CACHE BOOL "" FORCE)
set(SHADERC_ENABLE_HLSL OFF CACHE BOOL "" FORCE)

set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)

set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(SPIRV_HEADERS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(libs/shaderc EXCLUDE_FROM_ALL)

# Build/emit a single shared library named libshaderc.so (no ABI version suffix)
set_target_properties(shaderc_shared PROPERTIES OUTPUT_NAME shaderc)

# Project Sources
file(GLOB_RECURSE SOURCE_FILES src/*.cpp)
file(GLOB_RECURSE HEADER_FILES include/*.hpp)
set(CMAKE_CONFIGURE_DEPENDS ${ENGINE_SRC} ${HEADER_FILES})

add_executable(kingscraft
        ${SOURCE_FILES}
        ${HEADER_FILES}
)

target_link_directories(kingscraft PRIVATE)

target_link_libraries(kingscraft
        PRIVATE glfw
        PRIVATE vulkan
        PRIVATE shaderc_shared
        PRIVATE pthread
        PRIVATE dl
)

# Find the copied libshaderc.so next to the executable at runtime
set_target_properties(kingscraft PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH TRUE
)

add_custom_command(TARGET kingscraft POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:shaderc_shared>
        $<TARGET_FILE_DIR:kingscraft>
)

add_custom_command(TARGET kingscraft POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory $<TARGET_FILE_DIR:kingscraft>/resources
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources
        $<TARGET_FILE_DIR:kingscraft>/resources
)
