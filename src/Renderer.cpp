#include "Renderer.h"
#include <fstream>
#include <sstream>
#include <iostream>

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "Failed to open shader: " << path << "\n";
        return "";
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

GLuint Renderer::loadShader(const std::string& path, GLenum type) {
    std::string src = readFile(path);
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);

    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "Shader compile error (" << path << "):\n" << log << "\n";
    }
    return s;
}

GLuint Renderer::createProgram(const std::string& vertPath, const std::string& fragPath) {
    GLuint vs = loadShader(vertPath, GL_VERTEX_SHADER);
    GLuint fs = loadShader(fragPath, GL_FRAGMENT_SHADER);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::cerr << "Program link error:\n" << log << "\n";
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

static GLuint makeTexture(int w, int h, GLenum internalFormat) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    GLenum fmt = GL_RGBA, type = GL_FLOAT;
    if (internalFormat == GL_R32F) { fmt = GL_RED; }
    else if (internalFormat == GL_RG16F) { fmt = GL_RG; }
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, fmt, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

void Renderer::createTextures(int w, int h) {
    auto del = [](GLuint& t) { if (t) { glDeleteTextures(1, &t); t = 0; } };
    del(colorTex_); del(normalTex_); del(depthTex_);
    del(denoisePing_); del(denoisePong_);
    del(accumPing_); del(accumPong_);

    colorTex_     = makeTexture(w, h, GL_RGBA16F);
    normalTex_    = makeTexture(w, h, GL_RG16F);
    depthTex_     = makeTexture(w, h, GL_R32F);
    denoisePing_  = makeTexture(w, h, GL_RGBA16F);
    denoisePong_  = makeTexture(w, h, GL_RGBA16F);
    accumPing_    = makeTexture(w, h, GL_RGBA32F);
    accumPong_    = makeTexture(w, h, GL_RGBA32F);
}

static GLuint makeFBO(GLuint* attachments, int count) {
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    GLenum drawBuffers[8];
    for (int i = 0; i < count; ++i) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
                               GL_TEXTURE_2D, attachments[i], 0);
        drawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
    }
    glDrawBuffers(count, drawBuffers);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "FBO incomplete: 0x" << std::hex << status << std::dec << "\n";

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fbo;
}

void Renderer::createFBOs() {
    auto delFBO = [](GLuint& f) { if (f) { glDeleteFramebuffers(1, &f); f = 0; } };
    delFBO(ptFBO_); delFBO(denoisePingFBO_); delFBO(denoisePongFBO_);
    delFBO(accumPingFBO_); delFBO(accumPongFBO_);

    GLuint ptAttach[] = { colorTex_, normalTex_, depthTex_ };
    ptFBO_ = makeFBO(ptAttach, 3);

    denoisePingFBO_ = makeFBO(&denoisePing_, 1);
    denoisePongFBO_ = makeFBO(&denoisePong_, 1);
    accumPingFBO_   = makeFBO(&accumPing_, 1);
    accumPongFBO_   = makeFBO(&accumPong_, 1);
}

void Renderer::init(int width, int height) {
    width_ = width;
    height_ = height;

    ptProgram_      = createProgram("shaders/quad.vert", "shaders/pathtrace.frag");
    accumProgram_   = createProgram("shaders/quad.vert", "shaders/accumulate.frag");
    denoiseProgram_ = createProgram("shaders/quad.vert", "shaders/atrous.frag");
    quadProgram_    = createProgram("shaders/quad.vert", "shaders/quad.frag");

    createTextures(width, height);
    createFBOs();

    glGenVertexArrays(1, &quadVAO_);

    ptLoc_.cubeData    = glGetUniformLocation(ptProgram_, "uCubeData");
    ptLoc_.bvhData     = glGetUniformLocation(ptProgram_, "uBVHData");
    ptLoc_.cubeCount   = glGetUniformLocation(ptProgram_, "uCubeCount");
    ptLoc_.frameIndex  = glGetUniformLocation(ptProgram_, "uFrameIndex");
    ptLoc_.seed        = glGetUniformLocation(ptProgram_, "uSeed");
    ptLoc_.resolution  = glGetUniformLocation(ptProgram_, "uResolution");
    ptLoc_.sunDir      = glGetUniformLocation(ptProgram_, "uSunDir");
    ptLoc_.sunColor    = glGetUniformLocation(ptProgram_, "uSunColor");
    ptLoc_.skyZenith   = glGetUniformLocation(ptProgram_, "uSkyZenith");
    ptLoc_.skyHorizon  = glGetUniformLocation(ptProgram_, "uSkyHorizon");
    ptLoc_.ambient     = glGetUniformLocation(ptProgram_, "uAmbient");
    ptLoc_.emissiveIntensity = glGetUniformLocation(ptProgram_, "uEmissiveIntensity");
    ptLoc_.aoEnabled   = glGetUniformLocation(ptProgram_, "uAOEnabled");
    ptLoc_.aoStrength  = glGetUniformLocation(ptProgram_, "uAOStrength");
    ptLoc_.aoRadius    = glGetUniformLocation(ptProgram_, "uAORadius");
    ptLoc_.aoSamples   = glGetUniformLocation(ptProgram_, "uAOSamples");

    accumLoc_.newFrame    = glGetUniformLocation(accumProgram_, "uNewFrame");
    accumLoc_.accumFrame  = glGetUniformLocation(accumProgram_, "uAccumFrame");
    accumLoc_.blendFactor = glGetUniformLocation(accumProgram_, "uBlendFactor");

    denoiseLoc_.colorTex     = glGetUniformLocation(denoiseProgram_, "uColorTex");
    denoiseLoc_.normalTex    = glGetUniformLocation(denoiseProgram_, "uNormalTex");
    denoiseLoc_.depthTex     = glGetUniformLocation(denoiseProgram_, "uDepthTex");
    denoiseLoc_.stepWidth    = glGetUniformLocation(denoiseProgram_, "uStepWidth");
    denoiseLoc_.sigmaNormal  = glGetUniformLocation(denoiseProgram_, "uSigmaNormal");
    denoiseLoc_.invSigmaColor= glGetUniformLocation(denoiseProgram_, "uInvSigmaColor");
    denoiseLoc_.invSigmaDepth= glGetUniformLocation(denoiseProgram_, "uInvSigmaDepth");
    denoiseLoc_.texelSize    = glGetUniformLocation(denoiseProgram_, "uTexelSize");

    quadLoc_.texture    = glGetUniformLocation(quadProgram_, "uTexture");
    quadLoc_.exposure   = glGetUniformLocation(quadProgram_, "uExposure");
    quadLoc_.saturation = glGetUniformLocation(quadProgram_, "uSaturation");
}

void Renderer::resize(int width, int height) {
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;
    createTextures(width, height);
    createFBOs();
}

void Renderer::dispatchPathTrace(GLuint cameraUBO, GLuint cubeTBOTex, GLuint bvhTBOTex,
                                  int cubeCount, int frameIndex, unsigned int seed,
                                  const LightingParams& lighting, const AOParams& ao,
                                  float emissiveIntensity) {
    glBindFramebuffer(GL_FRAMEBUFFER, ptFBO_);
    glViewport(0, 0, width_, height_);

    glUseProgram(ptProgram_);

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, cameraUBO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, cubeTBOTex);
    glUniform1i(ptLoc_.cubeData, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, bvhTBOTex);
    glUniform1i(ptLoc_.bvhData, 1);

    glUniform1i(ptLoc_.cubeCount, cubeCount);
    glUniform1i(ptLoc_.frameIndex, frameIndex);
    glUniform1ui(ptLoc_.seed, seed);
    glUniform2f(ptLoc_.resolution, float(width_), float(height_));

    glUniform3fv(ptLoc_.sunDir,     1, lighting.sunDir);
    glUniform3fv(ptLoc_.sunColor,   1, lighting.sunColor);
    glUniform3fv(ptLoc_.skyZenith,  1, lighting.skyZenith);
    glUniform3fv(ptLoc_.skyHorizon, 1, lighting.skyHorizon);
    glUniform3fv(ptLoc_.ambient,    1, lighting.ambient);

    glUniform1f(ptLoc_.emissiveIntensity, emissiveIntensity);

    glUniform1i(ptLoc_.aoEnabled,   ao.enabled ? 1 : 0);
    glUniform1f(ptLoc_.aoStrength,  ao.strength);
    glUniform1f(ptLoc_.aoRadius,    ao.radius);
    glUniform1i(ptLoc_.aoSamples,   ao.samples);

    glBindVertexArray(quadVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::dispatchAccumulate(int frameIndex) {
    glUseProgram(accumProgram_);

    GLuint readTex  = accumWriteToPing_ ? accumPong_ : accumPing_;
    GLuint writeFBO = accumWriteToPing_ ? accumPingFBO_ : accumPongFBO_;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glUniform1i(accumLoc_.newFrame, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, readTex);
    glUniform1i(accumLoc_.accumFrame, 1);

    glUniform1f(accumLoc_.blendFactor, 1.0f / float(frameIndex));

    glBindFramebuffer(GL_FRAMEBUFFER, writeFBO);
    glViewport(0, 0, width_, height_);
    glBindVertexArray(quadVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    accumWriteToPing_ = !accumWriteToPing_;
}

GLuint Renderer::getAccumOutput() const {
    return accumWriteToPing_ ? accumPong_ : accumPing_;
}

void Renderer::dispatchDenoise(const DenoiseParams& params) {
    glUseProgram(denoiseProgram_);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, normalTex_);
    glUniform1i(denoiseLoc_.normalTex, 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, depthTex_);
    glUniform1i(denoiseLoc_.depthTex, 2);

    glUniform1f(denoiseLoc_.invSigmaColor, -1.0f / (params.sigmaColor + 1e-6f));
    glUniform1f(denoiseLoc_.sigmaNormal,  params.sigmaNormal);
    glUniform1f(denoiseLoc_.invSigmaDepth, -1.0f / (params.sigmaDepth + 1e-6f));
    glUniform2f(denoiseLoc_.texelSize, 1.0f / float(width_), 1.0f / float(height_));

    GLuint srcTex = getAccumOutput();
    GLuint dstFBO = denoisePingFBO_;
    lastOutputIsPing_ = true;

    glViewport(0, 0, width_, height_);
    glBindVertexArray(quadVAO_);

    for (int pass = 0; pass < params.atrousPasses; ++pass) {
        int stepWidth = 1 << pass;
        glUniform1i(denoiseLoc_.stepWidth, stepWidth);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, srcTex);
        glUniform1i(denoiseLoc_.colorTex, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, dstFBO);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        if (dstFBO == denoisePingFBO_) {
            srcTex = denoisePing_;
            dstFBO = denoisePongFBO_;
            lastOutputIsPing_ = true;
        } else {
            srcTex = denoisePong_;
            dstFBO = denoisePingFBO_;
            lastOutputIsPing_ = false;
        }
    }
}

GLuint Renderer::getOutputTexture() const {
    return lastOutputIsPing_ ? denoisePing_ : denoisePong_;
}

void Renderer::drawFullscreenQuad(float exposure, float saturation) {
    glUseProgram(quadProgram_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, getOutputTexture());
    glUniform1i(quadLoc_.texture, 0);
    glUniform1f(quadLoc_.exposure, exposure);
    glUniform1f(quadLoc_.saturation, saturation);

    glBindVertexArray(quadVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void Renderer::destroy() {
    glDeleteProgram(ptProgram_);
    glDeleteProgram(accumProgram_);
    glDeleteProgram(denoiseProgram_);
    glDeleteProgram(quadProgram_);
    glDeleteVertexArrays(1, &quadVAO_);

    auto del = [](GLuint& t) { if (t) { glDeleteTextures(1, &t); t = 0; } };
    del(colorTex_); del(normalTex_); del(depthTex_);
    del(denoisePing_); del(denoisePong_);
    del(accumPing_); del(accumPong_);

    auto delFBO = [](GLuint& f) { if (f) { glDeleteFramebuffers(1, &f); f = 0; } };
    delFBO(ptFBO_); delFBO(denoisePingFBO_); delFBO(denoisePongFBO_);
    delFBO(accumPingFBO_); delFBO(accumPongFBO_);
}
