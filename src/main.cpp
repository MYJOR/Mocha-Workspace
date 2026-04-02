#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "Renderer.h"
#include "Camera.h"
#include "ProceduralGen.h"

#include <iostream>
#include <cstdlib>
#include <cmath>
#include <algorithm>

static const int INIT_WIDTH  = 1280;
static const int INIT_HEIGHT = 720;

static float smoothstepf(float edge0, float edge1, float x) {
    float t = std::max(0.0f, std::min(1.0f, (x - edge0) / (edge1 - edge0)));
    return t * t * (3.0f - 2.0f * t);
}

static void lerpVec3(float* out, const float* a, const float* b, float t) {
    for (int i = 0; i < 3; ++i) out[i] = a[i] + (b[i] - a[i]) * t;
}

static void computeLighting(float azimuthDeg, float elevationDeg, LightingParams& lp) {
    constexpr float DEG2RAD = 3.14159265359f / 180.0f;
    float azRad = azimuthDeg * DEG2RAD;
    float elRad = elevationDeg * DEG2RAD;

    lp.sunDir[0] = std::cos(elRad) * std::sin(azRad);
    lp.sunDir[1] = std::sin(elRad);
    lp.sunDir[2] = std::cos(elRad) * std::cos(azRad);

    constexpr float nightZenith[]  = {0.01f, 0.01f, 0.04f};
    constexpr float nightHorizon[] = {0.03f, 0.03f, 0.06f};
    constexpr float nightSun[]     = {0.0f,  0.0f,  0.0f};
    constexpr float nightAmbient[] = {0.02f, 0.02f, 0.04f};

    constexpr float sunsetZenith[]  = {0.15f, 0.20f, 0.50f};
    constexpr float sunsetHorizon[] = {0.90f, 0.40f, 0.15f};
    constexpr float sunsetSun[]     = {2.50f, 1.00f, 0.30f};
    constexpr float sunsetAmbient[] = {0.10f, 0.08f, 0.06f};

    constexpr float dayZenith[]  = {0.60f, 0.75f, 0.95f};
    constexpr float dayHorizon[] = {0.85f, 0.85f, 0.90f};
    constexpr float daySun[]     = {2.50f, 2.20f, 1.80f};
    constexpr float dayAmbient[] = {0.15f, 0.18f, 0.25f};

    float horizonFade = smoothstepf(-10.0f, 0.0f, elevationDeg);
    float dayFade     = smoothstepf(0.0f, 30.0f, elevationDeg);

    float tmpZ[3], tmpH[3], tmpS[3], tmpA[3];
    lerpVec3(tmpZ, nightZenith,  sunsetZenith,  horizonFade);
    lerpVec3(tmpH, nightHorizon, sunsetHorizon, horizonFade);
    lerpVec3(tmpS, nightSun,     sunsetSun,     horizonFade);
    lerpVec3(tmpA, nightAmbient, sunsetAmbient, horizonFade);

    lerpVec3(lp.skyZenith,  tmpZ, dayZenith,  dayFade);
    lerpVec3(lp.skyHorizon, tmpH, dayHorizon, dayFade);
    lerpVec3(lp.sunColor,   tmpS, daySun,     dayFade);
    lerpVec3(lp.ambient,    tmpA, dayAmbient, dayFade);
}

struct AppState {
    Camera camera;
    Renderer renderer;
    ProceduralGen procGen;
    DenoiseParams denoiseParams;
    LightingParams lighting;

    int   gridSize    = 4;
    float noiseScale  = 0.12f;
    float heightScale = 5.0f;
    int   seed        = 42;

    float sunAzimuth   = 58.0f;
    float sunElevation = 52.0f;

    int  frameIndex   = 0;
    bool needRegen    = true;
};

static void framebufferSizeCallback(GLFWwindow* window, int w, int h) {
    if (w == 0 || h == 0) return;
    auto* app = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    glViewport(0, 0, w, h);
    app->renderer.resize(w, h);
    app->camera.update(w, h);
    app->camera.uploadUBO();
    app->frameIndex = 0;
}

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(INIT_WIDTH, INIT_HEIGHT,
                                           "Isometric Path Tracer", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "Failed to initialize GLAD\n";
        return 1;
    }

    std::cout << "OpenGL " << glGetString(GL_VERSION) << "\n"
              << "Renderer: " << glGetString(GL_RENDERER) << "\n";

    AppState app;
    glfwSetWindowUserPointer(window, &app);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    app.camera.init(INIT_WIDTH, INIT_HEIGHT);
    app.camera.uploadUBO();
    app.renderer.init(INIT_WIDTH, INIT_HEIGHT);
    computeLighting(app.sunAzimuth, app.sunElevation, app.lighting);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 410");
    ImGui::StyleColorsDark();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (app.needRegen) {
            app.procGen.generate(app.gridSize, app.noiseScale, app.heightScale, app.seed);
            app.procGen.uploadTBO();
            app.needRegen = false;
            app.frameIndex = 0;
        }

        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW == 0 || fbH == 0) continue;

        app.frameIndex++;
        unsigned int frameSeed = static_cast<unsigned int>(app.frameIndex * 719393 + 1);

        app.renderer.dispatchPathTrace(
            app.camera.getUBO(), app.procGen.getTBOTex(), app.procGen.getBVHTBOTex(),
            app.procGen.getCubeCount(), app.frameIndex, frameSeed, app.lighting
        );

        app.renderer.dispatchAccumulate(app.frameIndex);
        app.renderer.dispatchDenoise(app.denoiseParams);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbW, fbH);
        glClear(GL_COLOR_BUFFER_BIT);
        app.renderer.drawFullscreenQuad();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(320, 400), ImGuiCond_FirstUseEver);
        ImGui::Begin("Path Tracer Settings");

        ImGui::SeparatorText("Scene Generation");
        if (ImGui::SliderInt("Grid Size", &app.gridSize, 4, 48)) app.needRegen = true;
        if (ImGui::SliderFloat("Noise Scale", &app.noiseScale, 0.01f, 0.5f)) app.needRegen = true;
        if (ImGui::SliderFloat("Height Scale", &app.heightScale, 1.0f, 12.0f)) app.needRegen = true;
        if (ImGui::SliderInt("Seed", &app.seed, 0, 1000)) app.needRegen = true;
        ImGui::Text("Cubes: %d", app.procGen.getCubeCount());

        ImGui::SeparatorText("Camera");
        bool camDirty = false;
        camDirty |= ImGui::SliderFloat("Azimuth", &app.camera.azimuth, 0.0f, 360.0f);
        camDirty |= ImGui::SliderFloat("Elevation", &app.camera.elevation, 5.0f, 85.0f);
        camDirty |= ImGui::SliderFloat("Ortho Scale", &app.camera.orthoScale, 2.0f, 40.0f);
        camDirty |= ImGui::SliderFloat("Distance", &app.camera.distance, 5.0f, 80.0f);
        if (camDirty) {
            app.camera.update(fbW, fbH);
            app.camera.uploadUBO();
            app.frameIndex = 0;
        }

        ImGui::SeparatorText("Lighting");
        bool lightDirty = false;
        lightDirty |= ImGui::SliderFloat("Sun Azimuth", &app.sunAzimuth, 0.0f, 360.0f);
        lightDirty |= ImGui::SliderFloat("Sun Elevation", &app.sunElevation, -10.0f, 90.0f);
        if (lightDirty) {
            computeLighting(app.sunAzimuth, app.sunElevation, app.lighting);
            app.frameIndex = 0;
        }

        ImGui::SeparatorText("Denoiser (A-Trous)");
        ImGui::SliderFloat("Sigma Color", &app.denoiseParams.sigmaColor, 0.01f, 2.0f);
        ImGui::SliderFloat("Sigma Normal", &app.denoiseParams.sigmaNormal, 1.0f, 256.0f);
        ImGui::SliderFloat("Sigma Depth", &app.denoiseParams.sigmaDepth, 0.01f, 5.0f);
        ImGui::SliderInt("Passes", &app.denoiseParams.atrousPasses, 1, 7);

        ImGui::Separator();
        ImGui::Text("Frame: %d", app.frameIndex);
        ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);

        ImGui::End();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    app.renderer.destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
