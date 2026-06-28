cmake_minimum_required(VERSION 3.10)
project(VideoEditor)

set(CMAKE_CXX_STANDARD 17)

# Find OpenGL
find_package(OpenGL REQUIRED)

# Find GLFW using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(glfw REQUIRED glfw3)

# --- NEW: Find GLEW using pkg-config ---
pkg_check_modules(glew REQUIRED glew)

# --- NEW: Find FFmpeg libraries (Arch Linux requires libavcodec etc.) ---
pkg_check_modules(ffmpeg REQUIRED libavcodec libavformat libswscale libavutil)

# Set Include Directories
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
    third_party/imgui/ImGuiFileDialog.cpp # <--- ADD THIS FILE
)

add_executable(VideoEditor ${SOURCES})

# Link libraries (Include ffmpeg libraries here)
target_link_libraries(VideoEditor 
    ${glfw_LINK_LIBRARIES} 
    ${glew_LIBRARIES}
    ${ffmpeg_LIBRARIES} # <--- ADD FFmpeg linking
    OpenGL::GL
)
