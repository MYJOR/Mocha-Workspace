#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glad/glad.h>

struct CameraUBO {
    glm::mat4 invViewProj;
    glm::vec4 camPos;
    glm::vec4 camDir;
    glm::vec4 camUp;
    glm::vec4 camRight;
    glm::vec4 orthoExtents; // (halfW, halfH, near, far)
};

class Camera {
public:
    Camera();
    void init(int screenW, int screenH);
    void update(int screenW, int screenH);
    void uploadUBO() const;
    GLuint getUBO() const { return ubo_; }
    const CameraUBO& getData() const { return data_; }

    float distance = 20.0f;
    float azimuth  = 45.0f;
    float elevation = 35.264f; // arctan(1/sqrt(2)) for true isometric
    float orthoScale = 12.0f;

private:
    GLuint ubo_ = 0;
    CameraUBO data_{};
    int screenW_ = 1280, screenH_ = 720;
};
