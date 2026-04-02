#include "Camera.h"
#include <cmath>

Camera::Camera() {}

void Camera::init(int screenW, int screenH) {
    screenW_ = screenW;
    screenH_ = screenH;
    glGenBuffers(1, &ubo_);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(CameraUBO), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    update(screenW, screenH);
}

void Camera::update(int screenW, int screenH) {
    screenW_ = screenW;
    screenH_ = screenH;

    float azRad  = glm::radians(azimuth);
    float elRad  = glm::radians(elevation);

    glm::vec3 dir(
        cosf(elRad) * cosf(azRad),
        sinf(elRad),
        cosf(elRad) * sinf(azRad)
    );
    glm::vec3 eye = dir * distance;
    glm::vec3 center(0.0f, 0.0f, 0.0f);
    glm::vec3 up(0.0f, 1.0f, 0.0f);

    glm::mat4 view = glm::lookAt(eye, center, up);

    float aspect = float(screenW_) / float(screenH_);
    float halfH = orthoScale;
    float halfW = orthoScale * aspect;
    glm::mat4 proj = glm::ortho(-halfW, halfW, -halfH, halfH, 0.1f, 100.0f);

    glm::mat4 viewProj = proj * view;
    data_.invViewProj = glm::inverse(viewProj);
    data_.camPos  = glm::vec4(eye, 1.0f);
    data_.camDir  = glm::vec4(glm::normalize(center - eye), 0.0f);
    data_.camUp   = glm::vec4(up, 0.0f);
    data_.camRight = glm::vec4(glm::normalize(glm::cross(glm::vec3(data_.camDir), up)), 0.0f);
    data_.orthoExtents = glm::vec4(halfW, halfH, 0.1f, 100.0f);
}

void Camera::uploadUBO() const {
    glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CameraUBO), &data_);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}
