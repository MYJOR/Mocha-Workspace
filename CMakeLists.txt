cmake_minimum_required(VERSION 3.20)
project(IsometricPathTracer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

add_library(glad STATIC
    third_party/glad/src/glad.c
)
target_include_directories(glad PUBLIC third_party/glad/include)

add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/imgui_impl_glfw.cpp
    third_party/imgui/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC third_party/imgui)
target_link_libraries(imgui PUBLIC glfw)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/Renderer.cpp
    src/Camera.cpp
    src/ProceduralGen.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    third_party/stb
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    OpenGL::GL
    glfw
    glad
    glm::glm
    imgui
)

file(COPY shaders DESTINATION ${CMAKE_BINARY_DIR})
