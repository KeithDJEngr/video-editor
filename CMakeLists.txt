cmake_minimum_required(VERSION 3.10)
project(VideoEditor)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenGL REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(glfw REQUIRED glfw3)
pkg_check_modules(glew REQUIRED glew)
pkg_check_modules(ffmpeg REQUIRED libavcodec libavformat libswscale libavutil)

include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/third_party/imgui)
include_directories(${PROJECT_SOURCE_DIR}/third_party)
include_directories(${glfw_INCLUDE_DIRS})
include_directories(${glew_INCLUDE_DIRS})
include_directories(${ffmpeg_INCLUDE_DIRS})

set(SOURCES
    src/main.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
    third_party/imgui/ImGuiFileDialog.cpp
)

add_executable(VideoEditor ${SOURCES})

target_link_libraries(VideoEditor 
    ${glfw_LINK_LIBRARIES} 
    ${glew_LIBRARIES}
    ${ffmpeg_LIBRARIES}
    OpenGL::GL
    GLX
    EGL
)
