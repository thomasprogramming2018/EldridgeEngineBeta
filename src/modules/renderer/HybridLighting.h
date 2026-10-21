#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include "VulkanRayTracer.h"

// Two small history targets. Temporal reuse and denoising run on the GPU.
class HybridLighting
{
public:
    bool initialize(QOpenGLFunctions_3_3_Core& gl, QString& error);
    bool update(QOpenGLFunctions_3_3_Core& gl, const RayLightingFrame& frame, int historyLimit);
    void reset() { valid_ = false; }
    void release(QOpenGLFunctions_3_3_Core& gl);
    bool ready() const { return valid_; }
    bool fullFrame() const { return fullFrame_; }
    GLuint lighting() const { return historyLighting_[front_]; }
    GLuint surface() const { return historySurface_[front_]; }
    const Camera& camera() const { return camera_; }
    QSize size() const { return QSize(width_, height_); }
private:
    void releaseTargets(QOpenGLFunctions_3_3_Core& gl);
    QOpenGLShaderProgram program_;
    GLuint vao_ = 0;
    GLuint inputLighting_ = 0, inputSurface_ = 0;
    GLuint framebuffer_[2] {}, historyLighting_[2] {}, historySurface_[2] {};
    int width_ = 0, height_ = 0, front_ = 0;
    bool valid_ = false;
    bool fullFrame_ = false;
    Camera camera_;
};
