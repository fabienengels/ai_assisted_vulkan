cmake_minimum_required(VERSION 3.20)
project(bouncing_balls)

set(CMAKE_CXX_STANDARD 17)

find_package(Vulkan REQUIRED)

find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    find_library(GLFW_LIB glfw PATHS /opt/homebrew/lib REQUIRED)
    find_path(GLFW_INCLUDE GLFW/glfw3.h PATHS /opt/homebrew/include REQUIRED)
    add_library(glfw UNKNOWN IMPORTED)
    set_target_properties(glfw PROPERTIES
        IMPORTED_LOCATION ${GLFW_LIB}
        INTERFACE_INCLUDE_DIRECTORIES ${GLFW_INCLUDE})
endif()

find_path(GLM_INCLUDE glm/glm.hpp PATHS /opt/homebrew/include REQUIRED)

# Compile shaders
set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SPIRV_DIR  ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SPIRV_DIR})

set(GLSLANG glslangValidator)

add_custom_command(
    OUTPUT  ${SPIRV_DIR}/ball.vert.spv
    COMMAND ${GLSLANG} -V ${SHADER_DIR}/ball.vert -o ${SPIRV_DIR}/ball.vert.spv
    DEPENDS ${SHADER_DIR}/ball.vert)

add_custom_command(
    OUTPUT  ${SPIRV_DIR}/ball.frag.spv
    COMMAND ${GLSLANG} -V ${SHADER_DIR}/ball.frag -o ${SPIRV_DIR}/ball.frag.spv
    DEPENDS ${SHADER_DIR}/ball.frag)

add_custom_target(shaders DEPENDS
    ${SPIRV_DIR}/ball.vert.spv
    ${SPIRV_DIR}/ball.frag.spv)

add_executable(bouncing_balls main.cpp)
add_dependencies(bouncing_balls shaders)

target_include_directories(bouncing_balls PRIVATE
    ${GLM_INCLUDE}
    /opt/homebrew/include)

target_link_libraries(bouncing_balls PRIVATE
    Vulkan::Vulkan
    glfw)

# Pass shader directory as compile definition
target_compile_definitions(bouncing_balls PRIVATE
    SHADER_DIR="${SPIRV_DIR}/")
