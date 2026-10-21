#include "HybridLighting.h"
#include <algorithm>

bool HybridLighting::initialize(QOpenGLFunctions_3_3_Core& gl, QString& error)
{
    if (!program_.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/post.vert") ||
        !program_.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/temporal-lighting.frag") || !program_.link())
    {
        error = program_.log();
        return false;
    }
    gl.glGenVertexArrays(1, &vao_);
    return true;
}
void HybridLighting::releaseTargets(QOpenGLFunctions_3_3_Core& gl)
{
    if (inputLighting_) gl.glDeleteTextures(1, &inputLighting_);
    if (inputSurface_) gl.glDeleteTextures(1, &inputSurface_);
    gl.glDeleteTextures(2, historyLighting_);
    gl.glDeleteTextures(2, historySurface_);
    gl.glDeleteFramebuffers(2, framebuffer_);
    inputLighting_ = inputSurface_ = 0;
    for (int i = 0; i < 2; ++i) framebuffer_[i] = historyLighting_[i] = historySurface_[i] = 0;
    valid_ = false;
}
bool HybridLighting::update(QOpenGLFunctions_3_3_Core& gl, const RayLightingFrame& frame, int historyLimit)
{
    if (!vao_ || frame.width <= 0 || frame.height <= 0 ||
        frame.lighting.size() != frame.width * frame.height * 16 || frame.surface.size() != frame.lighting.size()) return false;
    if (fullFrame_ != frame.fullFrame) valid_ = false;
    fullFrame_ = frame.fullFrame;
    if (width_ != frame.width || height_ != frame.height || !inputLighting_)
    {
        releaseTargets(gl);
        width_ = frame.width; height_ = frame.height;
        auto createTexture = [&](GLuint& texture, GLint format)
        {
            gl.glGenTextures(1, &texture);
            gl.glBindTexture(GL_TEXTURE_2D, texture);
            gl.glTexImage2D(GL_TEXTURE_2D, 0, format, width_, height_, 0, GL_RGBA, GL_FLOAT, nullptr);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        };
        createTexture(inputLighting_, GL_RGBA32F);
        createTexture(inputSurface_, GL_RGBA32F);
        gl.glGenFramebuffers(2, framebuffer_);
        for (int i = 0; i < 2; ++i)
        {
            createTexture(historyLighting_[i], GL_RGBA16F);
            createTexture(historySurface_[i], GL_RGBA32F);
            gl.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_[i]);
            gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, historyLighting_[i], 0);
            gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, historySurface_[i], 0);
            if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                releaseTargets(gl);
                return false;
            }
        }
    }
    gl.glActiveTexture(GL_TEXTURE0);
    gl.glBindTexture(GL_TEXTURE_2D, inputLighting_);
    gl.glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RGBA, GL_FLOAT, frame.lighting.constData());
    gl.glBindTexture(GL_TEXTURE_2D, inputSurface_);
    gl.glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RGBA, GL_FLOAT, frame.surface.constData());
    int next = 1 - front_;
    gl.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_[next]);
    GLenum buffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    gl.glDrawBuffers(2, buffers);
    gl.glViewport(0, 0, width_, height_);
    gl.glDisable(GL_BLEND);
    gl.glDisable(GL_SCISSOR_TEST);
    gl.glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.glDisable(GL_DEPTH_TEST);
    program_.bind();
    GLuint textures[] = {inputLighting_, inputSurface_, historyLighting_[front_], historySurface_[front_]};
    const char* names[] = {"currentLighting", "currentSurface", "oldLighting", "oldSurface"};
    for (int i = 0; i < 4; ++i)
    {
        gl.glActiveTexture(GL_TEXTURE0 + i);
        gl.glBindTexture(GL_TEXTURE_2D, textures[i]);
        program_.setUniformValue(names[i], i);
    }
    auto viewProjection = frame.camera.projectionMatrix(width_, height_) * frame.camera.viewMatrix();
    auto oldViewProjection = camera_.projectionMatrix(width_, height_) * camera_.viewMatrix();
    program_.setUniformValue("inverseViewProjection", viewProjection.inverted());
    program_.setUniformValue("oldViewProjection", oldViewProjection);
    program_.setUniformValue("eye", frame.camera.position);
    program_.setUniformValue("oldEye", camera_.position);
    program_.setUniformValue("historyValid", valid_);
    program_.setUniformValue("fullFrame", fullFrame_);
    program_.setUniformValue("cumulative", frame.cumulative);
    bool moving = (frame.camera.position - camera_.position).lengthSquared() > 0.000001f ||
                  QVector3D::dotProduct(frame.camera.forward(), camera_.forward()) < 0.99999f;
    // Reflections depend on the view direction. Keep their history short in motion.
    int limit = moving ? std::min(historyLimit, fullFrame_ ? 2 : 4) : historyLimit;
    program_.setUniformValue("historyLimit", float(std::clamp(limit, 1, 512)));
    gl.glBindVertexArray(vao_);
    gl.glDrawArrays(GL_TRIANGLES, 0, 3);
    gl.glBindVertexArray(0);
    program_.release();
    front_ = next; camera_ = frame.camera; valid_ = true;
    return true;
}
void HybridLighting::release(QOpenGLFunctions_3_3_Core& gl)
{
    releaseTargets(gl);
    if (vao_) gl.glDeleteVertexArrays(1, &vao_);
    vao_ = 0;
    program_.removeAllShaders();
}
