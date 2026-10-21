#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include "RenderSettings.h"
#include "camera.h"
#include "HybridLighting.h"

// HDR scene targets, followed by one screen-space composition pass.
class PostProcessor
{
public:
    bool initialize(QOpenGLFunctions_3_3_Core& gl, QString& error);
    bool begin(QOpenGLFunctions_3_3_Core& gl, int width, int height);
    void finish(QOpenGLFunctions_3_3_Core& gl, GLuint destination,
                const Camera& camera, const RenderSettings& settings);
    void release(QOpenGLFunctions_3_3_Core& gl);
    void setLightingFrame(const RayLightingFrame& frame) { pendingLighting_ = frame; }
    void resetLighting() { pendingLighting_ = {}; hybrid_.reset(); }
private:
    void releaseTargets(QOpenGLFunctions_3_3_Core& gl);
    QOpenGLShaderProgram program_;
    HybridLighting hybrid_;
    RayLightingFrame pendingLighting_;
    GLuint framebuffer_ = 0;
    GLuint color_ = 0;
    GLuint surface_ = 0;
    GLuint depth_ = 0;
    GLuint vao_ = 0;
    int width_ = 0;
    int height_ = 0;
};
