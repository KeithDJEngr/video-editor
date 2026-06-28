#include <iostream>
#include <filesystem> // C++17 file system
#include <vector>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ImGui & Dialog
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "ImGuiFileDialog.h" // Our custom file dialog

// stb_image (For Image files)
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

// FFmpeg Headers (C-style, wrapped in extern C for C++ compatibility)
extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
}

// Define constant macros required by FFmpeg in C++
#define __STDC_CONSTANT_MACROS
#ifdef _WIN32
#define INLINE __inline
#endif

// --- Shader Source (Updated to support solid color) ---
const char* vertexShaderSource = "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec2 aTexCoords;\n"
    "out vec2 TexCoords;\n"
    "void main() {\n"
    "   gl_Position = vec4(aPos, 1.0);\n"
    "   TexCoords = aTexCoords;\n"
    "}\n";

// Added 'vec3 objectColor' to handle the white fallback state
const char* fragmentShaderSource = "#version 330 core\n"
    "in vec2 TexCoords;\n"
    "uniform sampler2D videoFrame;\n"
    "uniform bool useTexture;\n" // New uniform: if true, sample texture, else use color
    "uniform vec3 objectColor;\n" // New uniform: fallback color (white)
    "out vec4 FragColor;\n"
    "void main() {\n"
    "   if (useTexture) {\n"
    "       FragColor = texture(videoFrame, TexCoords);\n"
    "   } else {\n"
    "       FragColor = vec4(objectColor, 1.0);\n"
    "   }\n"
    "}\n";

// --- FFmpeg Player State Struct ---
struct VideoPlayer {
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* dec_ctx = nullptr;
    int video_stream_idx = -1;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    
    // For texture update
    struct SwsContext* sws_ctx = nullptr;
    unsigned char* pixels = nullptr;
    unsigned char* rgbaPixels = nullptr;
    int width = 0, height = 0;

    void init(const char* path) {
        // 1. Open File
        if (avformat_open_input(&fmt_ctx, path, nullptr, nullptr) < 0) {
            std::cout << "Could not open file." << std::endl;
            return;
        }

        // 2. Find Stream Info
        if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
            std::cout << "Could not find stream info." << std::endl;
            close();
            return;
        }

        // 3. Find Video Stream
        for (int i = 0; i < (int)fmt_ctx->nb_streams; i++) {
            if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                video_stream_idx = i;
                break;
            }
        }

        if (video_stream_idx == -1) {
            std::cout << "Did not find a video stream." << std::endl;
            close();
            return;
        }

        // 4. Get Codec & Open
        AVCodecParameters* codecpar = fmt_ctx->streams[video_stream_idx]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
        if (!codec) {
            std::cout << "Unsupported codec." << std::endl;
            close();
            return;
        }

        dec_ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(dec_ctx, codecpar);
        if (avcodec_open2(dec_ctx, codec, nullptr) < 0) {
            std::cout << "Could not open codec." << std::endl;
            close();
            return;
        }

        // 5. Allocate Frame/Packet
        frame = av_frame_alloc();
        packet = av_packet_alloc();

        // 6. Setup SwsContext for RGB conversion (OpenGL needs RGB)
        width = dec_ctx->width;
        height = dec_ctx->height;
        pixels = new unsigned char[width * height * 3]; // Allocate buffer for RGB24
        
        rgbaPixels = new unsigned char[width * height * 4];
        
        sws_ctx = sws_getContext(width, height, dec_ctx->pix_fmt, 
                                 width, height, AV_PIX_FMT_RGB24, 
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);
        
        // Decode first frame so texture has valid data on load
        if (decodeFirstFrame()) {
            std::cout << "[VIDEO] First frame decoded (" << width << "x" << height << ")" << std::endl;
        } else {
            std::cout << "[VIDEO] WARNING: Could not decode first frame!" << std::endl;
        }
        
        std::cout << "[VIDEO] Video loaded: " << path << " (" << width << "x" << height << ")" << std::endl;
    }

    bool decodeFirstFrame() {
        if (!fmt_ctx || !dec_ctx || !frame || !packet || !sws_ctx || !pixels) return false;
        av_seek_frame(fmt_ctx, video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec_ctx);
        int gotFrame = 0;
        int packetsRead = 0;
        while (av_read_frame(fmt_ctx, packet) >= 0 && packetsRead < 100) {
            packetsRead++;
            if (packet->stream_index != video_stream_idx) {
                av_packet_unref(packet);
                continue;
            }
            if (avcodec_send_packet(dec_ctx, packet) < 0) {
                av_packet_unref(packet);
                continue;
            }
            int ret = avcodec_receive_frame(dec_ctx, frame);
            if (ret == 0) {
                int dstStride[1] = { width * 3 };
                sws_scale(sws_ctx, (const uint8_t* const*)frame->data, frame->linesize, 0, height, &pixels, dstStride);
                gotFrame = 1;
                break;
            }
            av_packet_unref(packet);
        }
        if (gotFrame) {
            // First frame already has data in pixels buffer - no conversion needed
            // (RGB24 matches GL_RGB format)
        }
        return gotFrame;
    }

    bool updateFrame() {
        int framesDecoded = 0;
        for (int i = 0; i < 10; i++) {
            int ret = avcodec_receive_frame(dec_ctx, frame);
            if (ret == 0) {
                int dstStride[1] = { width * 3 };
                sws_scale(sws_ctx, (const uint8_t* const*)frame->data, frame->linesize, 0, height, &pixels, dstStride);
                framesDecoded++;
                continue;
            }
            if (av_read_frame(fmt_ctx, packet) < 0) {
                avcodec_send_packet(dec_ctx, nullptr);
                av_seek_frame(fmt_ctx, -1, 0, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(dec_ctx);
                break;
            }
            if (packet->stream_index != video_stream_idx) {
                av_packet_unref(packet);
                continue;
            }
            if (avcodec_send_packet(dec_ctx, packet) < 0) {
                av_packet_unref(packet);
                continue;
            }
            ret = avcodec_receive_frame(dec_ctx, frame);
            if (ret == 0) {
                int dstStride[1] = { width * 3 };
                sws_scale(sws_ctx, (const uint8_t* const*)frame->data, frame->linesize, 0, height, &pixels, dstStride);
                framesDecoded++;
            }
            av_packet_unref(packet);
        }
        if (framesDecoded > 0) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
        }
        return framesDecoded > 0;
    }

    void close() {
        if (sws_ctx) sws_freeContext(sws_ctx);
        delete[] pixels;
        delete[] rgbaPixels;
        pixels = nullptr;
        rgbaPixels = nullptr;
        if (dec_ctx) {
            avcodec_free_context(&dec_ctx);
            dec_ctx = nullptr;
        }
        if (frame) av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        if (fmt_ctx) avformat_close_input(&fmt_ctx);
    }
};

// --- Application State ---
struct AppState {
    bool isPlaying = false;
    unsigned int videoTextureID = 0;
    unsigned int shaderProgram = 0;
    unsigned int quadVAO, quadVBO;
    
    VideoPlayer player;
    enum class Mode { IMAGE, VIDEO, WHITE };
    Mode currentMode = Mode::WHITE; // Default to white
    GLint useTextureLoc = -1;
    GLint objectColorLoc = -1;

    void initShader() {
        unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
        glCompileShader(vertexShader);
        
        unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
        glCompileShader(fragmentShader);
        
        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);
        
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        useTextureLoc = glGetUniformLocation(shaderProgram, "useTexture");
        objectColorLoc = glGetUniformLocation(shaderProgram, "objectColor");
    }

    void setupQuad() {
        // Setup quad with correct aspect ratio based on content
        setupQuadForAspect(16.0f / 9.0f); // Default 16:9
    }
    
    void setupQuadForAspect(float contentAspect) {
        float windowAspect = 1280.0f / 720.0f;
        
        float quadWidth, quadHeight;
        if (contentAspect > windowAspect) {
            quadWidth = 2.0f;
            quadHeight = 2.0f * windowAspect / contentAspect;
        } else {
            quadHeight = 2.0f;
            quadWidth = 2.0f * contentAspect / windowAspect;
        }
        
        float hw = quadWidth * 0.5f;
        float hh = quadHeight * 0.5f;
        
        // Two triangles covering the full quad, explicit layout
        float quadVertices[] = {
            -hw,  hh, 0.0f,   0.0f, 0.0f,  // top-left
             hw,  hh, 0.0f,   1.0f, 0.0f,  // top-right
            -hw, -hh, 0.0f,   0.0f, 1.0f,  // bottom-left
             hw, -hh, 0.0f,   1.0f, 1.0f,  // bottom-right
            -hw,  hh, 0.0f,   0.0f, 0.0f,  // top-left (dup)
             hw, -hh, 0.0f,   1.0f, 1.0f,  // bottom-right (dup)
        };
        
        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); // Pos
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1); // UV
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    }

    void loadFile(const char* path) {
        std::string ext = std::filesystem::path(path).extension().string();
        std::cout << "[LOAD] Loading file: " << path << " (ext: " << ext << ")" << std::endl;
        
        // Reset state
        if (currentMode == Mode::VIDEO) player.close();
        glDeleteTextures(1, &videoTextureID);
        videoTextureID = 0;

        if (ext == ".jpg" || ext == ".png" || ext == ".jpeg" || ext == ".bmp") {
            int imgW, imgH, nrChannels;
            unsigned char* data = stbi_load(path, &imgW, &imgH, &nrChannels, 3);
            
            if (data) {
                glGenTextures(1, &videoTextureID);
                glBindTexture(GL_TEXTURE_2D, videoTextureID);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, imgW, imgH, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                stbi_image_free(data);
                
                float aspect = (float)imgW / imgH;
                setupQuadForAspect(aspect);
                std::cout << "[LOAD] Image loaded: " << imgW << "x" << imgH << " (aspect: " << aspect << "), quad updated" << std::endl;
                currentMode = Mode::IMAGE;
            } else {
                std::cout << "[LOAD] stbi_load failed for: " << path << std::endl;
                currentMode = Mode::WHITE;
            }
        } 
        else if (ext == ".mp4" || ext == ".avi" || ext == ".mov") {
            player.init(path);
            
            if (player.frame != nullptr && player.pixels != nullptr) {
                glGenTextures(1, &videoTextureID);
                glBindTexture(GL_TEXTURE_2D, videoTextureID);
                
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                
                // Test A: upload as GL_RGB raw pixels (3 bytes/pixel) - same as image path
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, player.width, player.height, 0, GL_RGB, GL_UNSIGNED_BYTE, player.pixels);
                glFinish();
                std::vector<GLubyte> testRgb(3);
                glReadPixels(player.width/2, player.height/2, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, testRgb.data());
                std::cout << "[LOAD] TEST-A (GL_RGB raw): center=" << (int)testRgb[0] << "," << (int)testRgb[1] << "," << (int)testRgb[2] << std::endl;
                
                // Test B: upload as GL_RGBA rgbaPixels (4 bytes/pixel)
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, player.width, player.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, player.rgbaPixels);
                glFinish();
                std::vector<GLubyte> testRgba(4);
                glReadPixels(player.width/2, player.height/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, testRgba.data());
                std::cout << "[LOAD] TEST-B (GL_RGBA conv): center=" << (int)testRgba[0] << "," << (int)testRgba[1] << "," << (int)testRgba[2] << std::endl;
                
                // Also print raw pixel values at known offset
                int centerOffset = (player.height/2) * player.width * 3 + (player.width/2) * 3;
                std::cout << "[LOAD] pixels[" << centerOffset << "] = " << (int)player.pixels[centerOffset] << ","
                          << (int)player.pixels[centerOffset+1] << "," << (int)player.pixels[centerOffset+2] << std::endl;
                
                float aspect = (float)player.width / player.height;
                setupQuadForAspect(aspect);
                std::cout << "[LOAD] Video texture created: " << player.width << "x" << player.height << " (aspect: " << aspect << ")" << std::endl;
                
                currentMode = Mode::VIDEO;
            } else {
                std::cout << "[LOAD] Video init failed - frame=" << (int)(player.frame != nullptr) << " pixels=" << (int)(player.pixels != nullptr) << std::endl;
                currentMode = Mode::WHITE;
            }
        } 
        else {
            std::cout << "[LOAD] Unsupported file type: " << ext << std::endl;
            currentMode = Mode::WHITE;
        }
    }
    
    void cleanup() {
        player.close();
        glDeleteTextures(1, &videoTextureID);
        glDeleteProgram(shaderProgram);
        glDeleteVertexArrays(1, &quadVAO);
        glDeleteBuffers(1, &quadVBO);
    }
};

int main() {
    if (!glfwInit()) return -1;
    
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Video Editor", NULL, NULL);
    if (!window) { return -1; }
    glfwMakeContextCurrent(window);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { return -1; }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    AppState state;
    state.initShader();
    state.setupQuad();

    // --- Main Loop ---
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 1. Render Video/Texture Area
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        glUseProgram(state.shaderProgram);
        
        // Update Shader Uniforms based on state
        if (state.currentMode == AppState::Mode::WHITE) {
            glUniform1i(state.useTextureLoc, 0);
            glUniform3f(state.objectColorLoc, 1.0f, 1.0f, 1.0f); // WHITE
        } else if (state.currentMode == AppState::Mode::VIDEO) {
            glUniform1i(state.useTextureLoc, 1);
            glUniform3f(state.objectColorLoc, 1.0f, 0.0f, 0.0f); // RED fallback
        } else if (state.currentMode == AppState::Mode::IMAGE) {
            glUniform1i(state.useTextureLoc, 1);
            glUniform3f(state.objectColorLoc, 0.0f, 1.0f, 0.0f); // GREEN fallback
        }

        if (state.isPlaying && state.currentMode == AppState::Mode::VIDEO) {
            state.player.updateFrame();
        }

        if (state.videoTextureID) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, state.videoTextureID);
            
            GLint boundTex = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTex);
            static int debugCounter2 = 0;
            debugCounter2++;
            if (debugCounter2 % 300 == 0) {
                std::cout << "[RENDER] boundTex=" << boundTex << " videoTextureID=" << state.videoTextureID << std::endl;
            }
        } else {
            if (state.currentMode != AppState::Mode::WHITE) {
                std::cout << "[RENDER] NO TEXTURE bound (mode=" << (int)state.currentMode << ")" << std::endl;
            }
        }

        glBindVertexArray(state.quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        // Debug info
        static int frameCount = 0;
        static int debugCounter = 0;
        frameCount++;
        debugCounter++;
        if (debugCounter % 300 == 0 && state.currentMode == AppState::Mode::VIDEO) {
            // Check if texture has non-zero data by reading back a pixel
            GLint texW = 0, texH = 0;
            glBindTexture(GL_TEXTURE_2D, state.videoTextureID);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &texW);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &texH);
            
            // Read a pixel from the texture (center)
            std::vector<GLubyte> pixelData(3);
            glReadPixels(texW/2, texH/2, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixelData.data());
            
            GLenum err = glGetError();
            const char* modeStr = state.currentMode == AppState::Mode::VIDEO ? "VIDEO" : 
                                   state.currentMode == AppState::Mode::IMAGE ? "IMAGE" : "UNKNOWN";
            
            std::cout << "[DEBUG] Frame " << frameCount << " | Mode: " << modeStr << " | Texture: " << texW << "x" << texH 
                      << " | GL error: " << (err ? std::to_string(err) : "none")
                      << " | Pixel center: [" << (int)pixelData[0] << "," << (int)pixelData[1] << "," << (int)pixelData[2] << "]"
                      << " | Player pixels[0] = " << (int)state.player.pixels[0]
                      << std::endl;
        }

        // 2. UI Overlay
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1280, 300));
        ImGui::Begin("Editor Controls");
        
        static char filePath[512] = "";

	        // ... inside main loop ...

        // File Browser Button
        if (ImGui::Button("Select Video/Image")) {
            IGFD::FileDialog::Instance()->OpenDialog(
                "ChooseFileDlgKey",
                "Choose a file",
                ".jpg,.png,.mp4,.avi",
                IGFD::FileDialogConfig{.path = "."}
            );
        }

        // Display Dialog
        if (IGFD::FileDialog::Instance()->Display("ChooseFileDlgKey")) {
            if (IGFD::FileDialog::Instance()->IsOk()) {
                std::string selectedPath = IGFD::FileDialog::Instance()->GetFilePathName();

                strncpy(filePath, selectedPath.c_str(), 511);
                filePath[511] = '\0';

                state.loadFile(filePath);
            }
            IGFD::FileDialog::Instance()->Close();
        }


        // Playback Controls
        if (state.currentMode != AppState::Mode::WHITE) {
            if (ImGui::Button(state.isPlaying ? "Pause" : "Play")) {
                state.isPlaying = !state.isPlaying;
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop")) {
                state.isPlaying = false;
            }
        }
        
        const char* modeStr = "WHITE";
        if (state.currentMode == AppState::Mode::VIDEO) modeStr = "VIDEO";
        else if (state.currentMode == AppState::Mode::IMAGE) modeStr = "IMAGE";
        
        ImGui::TextColored(ImVec4(1, 0.5, 0.5, 1), "Current File: %s", filePath);
        ImGui::TextColored(ImVec4(0.5, 1, 0.5, 1), "Mode: %s", modeStr);
        if (state.videoTextureID > 0) {
            GLint texW = 0, texH = 0;
            glBindTexture(GL_TEXTURE_2D, state.videoTextureID);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &texW);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &texH);
            ImGui::TextColored(ImVec4(0.8, 0.8, 1.0, 1), "Texture: %dx%d, ID: %u", texW, texH, state.videoTextureID);
        } else {
            ImGui::TextColored(ImVec4(1, 0.3, 0.3, 1), "No texture loaded");
        }
        if (state.currentMode == AppState::Mode::VIDEO) {
            ImGui::Text("Player: w=%d h=%d stream=%d", state.player.width, state.player.height, state.player.video_stream_idx);
            ImGui::Text("Frame=%s Packet=%s", state.player.frame ? "YES" : "NULL", state.player.packet ? "YES" : "NULL");
        }

        // Timeline UI
        ImGui::Separator();
        ImGui::Text("Timeline (Track 1)");
        static float trackProgress[3] = {0.4f, 0.7f, 0.5f};
        if (ImGui::SliderFloat("Clip Start", &trackProgress[0], 0.0f, 1.0f)) {}
        if (ImGui::SliderFloat("Clip End", &trackProgress[1], trackProgress[0], 1.0f)) {}
        if (ImGui::SliderFloat("Opacity", &trackProgress[2], 0.0f, 1.0f)) {}

        ImGui::End();

        // Render Frame
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    state.cleanup();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
    return 0;
}
