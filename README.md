# Dependencies installation
sudo pacman -S ffmpeg glfw glad glm stb libgl glew unzip curl

# Included files
mkdir -p third_party/imgui/backends

## 1. Fetch a stable release (v1.90 is current recommended)
curl -L https://github.com/ocornut/imgui/archive/refs/tags/v1.90.zip -o imgui_temp.zip
unzip imgui_temp.zip -d /tmp && rm imgui_temp.zip

## 2. Copy core ImGui files
cp /tmp/imgui-1.90/*.h third_party/imgui/
cp /tmp/imgui-1.90/imgui*.cpp third_party/imgui/

## 3. Copy platform backend implementations (GLFW + OpenGL3)
cp /tmp/imgui-1.90/backends/*.h third_party/imgui/backends/
cp /tmp/imgui-1.90/backends/*.cpp third_party/imgui/backends/

rm -rf /tmp/imgui-1.90

curl -L https://raw.githubusercontent.com/ocornut/imgui/v1.90/imgui_tables.cpp -o third_party/imgui/imgui_tables.cpp

cd third_party/imgui
curl -L https://raw.githubusercontent.com/juliettef/IconFontCppHeaders/master/ImGuiFileDialog.h -o ImGuiFileDialog.h
curl -L https://raw.githubusercontent.com/juliettef/IconFontCppHeaders/master/ImGuiFileDialog.cpp -o ImGuiFileDialog.cpp
cd ../..


# Remove old/broken files
rm -f third_party/imgui/ImGuiFileDialog.*
rm -f third_party/stb_image.h

# Download ImGuiFileDialog (repo: aiekick/ImGuiFileDialog, branch: master)
curl -L https://raw.githubusercontent.com/aiekick/ImGuiFileDialog/master/ImGuiFileDialog.h -o third_party/imgui/ImGuiFileDialog.h
curl -L https://raw.githubusercontent.com/aiekick/ImGuiFileDialog/master/ImGuiFileDialog.cpp -o third_party/imgui/ImGuiFileDialog.cpp
curl -L https://raw.githubusercontent.com/aiekick/ImGuiFileDialog/master/ImGuiFileDialogConfig.h -o third_party/imgui/ImGuiFileDialogConfig.h

# Download stb_image
curl -L https://raw.githubusercontent.com/nothings/stb/master/stb_image.h -o third_party/stb_image.h

# Verify the file content is correct (should start with #ifndef, not 404)
head -n 3 third_party/imgui/ImGuiFileDialog.h


# Build
rm -rf build/ && mkdir -p build && cd build
cmake ..
make -j4
./VideoEditor
