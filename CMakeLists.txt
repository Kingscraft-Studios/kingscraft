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
        PRIVATE pthread
        PRIVATE dl
)

add_custom_command(TARGET kingscraft POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory $<TARGET_FILE_DIR:kingscraft>/resources
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources
        $<TARGET_FILE_DIR:kingscraft>/resources
)
