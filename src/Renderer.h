#pragma once
#include <glad/glad.h>
#include <string>

struct DenoiseParams {
    float sigmaColor  = 0.45f;
    float sigmaNormal = 128.0f;
    float sigmaDepth  = 0.8f;
    int   atrousPasses = 5;
};

struct LightingParams {
    float sunDir[3]     = {0.524f, 0.786f, 0.327f};
    float sunColor[3]   = {2.5f, 2.2f, 1.8f};
    float skyZenith[3]  = {0.6f, 0.75f, 0.95f};
    float skyHorizon[3] = {0.85f, 0.85f, 0.9f};
    float ambient[3]    = {0.15f, 0.18f, 0.25f};
};

class Renderer {
public:
    void init(int width, int height);
    void resize(int width, int height);
    void dispatchPathTrace(GLuint cameraUBO, GLuint cubeTBOTex, GLuint bvhTBOTex,
                           int cubeCount, int frameIndex, unsigned int seed,
                           const LightingParams& lighting);
    void dispatchAccumulate(int frameIndex);
    void dispatchDenoise(const DenoiseParams& params);
    void drawFullscreenQuad(float exposure);
    void destroy();

    GLuint getOutputTexture() const;
    GLuint getAccumOutput() const;

private:
    GLuint loadShader(const std::string& path, GLenum type);
    GLuint createProgram(const std::string& vertPath, const std::string& fragPath);
    void createTextures(int w, int h);
    void createFBOs();

    int width_ = 0, height_ = 0;

    GLuint ptProgram_      = 0;
    GLuint accumProgram_   = 0;
    GLuint denoiseProgram_ = 0;
    GLuint quadProgram_    = 0;

    GLuint colorTex_    = 0;
    GLuint normalTex_   = 0;
    GLuint depthTex_    = 0;
    GLuint denoisePing_ = 0;
    GLuint denoisePong_ = 0;

    GLuint ptFBO_          = 0;
    GLuint denoisePingFBO_ = 0;
    GLuint denoisePongFBO_ = 0;

    GLuint accumPing_      = 0;
    GLuint accumPong_      = 0;
    GLuint accumPingFBO_   = 0;
    GLuint accumPongFBO_   = 0;
    bool   accumWriteToPing_ = true;

    GLuint quadVAO_ = 0;
    bool lastOutputIsPing_ = true;

    struct {
        GLint cubeData, bvhData, cubeCount, frameIndex, seed, resolution;
        GLint sunDir, sunColor, skyZenith, skyHorizon, ambient;
    } ptLoc_{};

    struct {
        GLint newFrame, accumFrame, blendFactor;
    } accumLoc_{};

    struct {
        GLint colorTex, normalTex, depthTex;
        GLint stepWidth, sigmaNormal, invSigmaColor, invSigmaDepth, texelSize;
    } denoiseLoc_{};

    struct {
        GLint texture, exposure;
    } quadLoc_{};
};
