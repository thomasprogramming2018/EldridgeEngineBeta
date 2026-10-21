#include "PostProcessor.h"

bool PostProcessor::initialize(QOpenGLFunctions_3_3_Core& gl, QString& error)
{
    if (!program_.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/post.vert") ||
        !program_.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/post.frag") || !program_.link())
    { error = program_.log(); return false; }
    if (!hybrid_.initialize(gl, error)) return false;
    gl.glGenVertexArrays(1, &vao_);
    return true;
}
void PostProcessor::releaseTargets(QOpenGLFunctions_3_3_Core& gl)
{
    if (color_) gl.glDeleteTextures(1, &color_);
    if (surface_) gl.glDeleteTextures(1, &surface_);
    if (depth_) gl.glDeleteTextures(1, &depth_);
    if (framebuffer_) gl.glDeleteFramebuffers(1, &framebuffer_);
    color_ = surface_ = depth_ = framebuffer_ = 0;
}
bool PostProcessor::begin(QOpenGLFunctions_3_3_Core& gl, int width, int height)
{
    if (!vao_) return false;
    if (width != width_ || height != height_ || !framebuffer_)
    {
        releaseTargets(gl);
        width_ = width; height_ = height;
        gl.glGenFramebuffers(1, &framebuffer_);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        GLuint* textures[] = {&color_, &surface_, &depth_};
        for (int i = 0; i < 3; ++i)
        {
            gl.glGenTextures(1, textures[i]);
            gl.glBindTexture(GL_TEXTURE_2D, *textures[i]);
            gl.glTexImage2D(GL_TEXTURE_2D, 0, i == 2 ? GL_DEPTH_COMPONENT24 : GL_RGBA16F,
                width, height, 0, i == 2 ? GL_DEPTH_COMPONENT : GL_RGBA, GL_FLOAT, nullptr);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            gl.glFramebufferTexture2D(GL_FRAMEBUFFER, i == 2 ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0 + i,
                                     GL_TEXTURE_2D, *textures[i], 0);
        }
        GLenum buffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        gl.glDrawBuffers(2, buffers);
        if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        { releaseTargets(gl); return false; }
    }
    gl.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    gl.glViewport(0, 0, width_, height_);
    gl.glEnable(GL_DEPTH_TEST);
    GLenum buffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    gl.glDrawBuffers(2, buffers);
    const GLfloat emptySurface[] = {0, 0, 0, 1};
    gl.glClearBufferfv(GL_COLOR, 1, emptySurface);
    return true;
}
void PostProcessor::finish(QOpenGLFunctions_3_3_Core& gl, GLuint destination,
                           const Camera& camera, const RenderSettings& settings)
{
    if (settings.hardwareRayTracing && pendingLighting_.width)
    {
        hybrid_.update(gl, pendingLighting_, settings.raySamples);
        pendingLighting_ = {};
    }
    gl.glBindFramebuffer(GL_FRAMEBUFFER, destination);
    gl.glViewport(0, 0, width_, height_);
    gl.glDisable(GL_BLEND);
    gl.glDisable(GL_SCISSOR_TEST);
    gl.glDisable(GL_DEPTH_TEST);
    program_.bind();
    GLuint textures[] = {color_, surface_, depth_};
    const char* names[] = {"sceneColor", "surfaceData", "sceneDepth"};
    for (int i = 0; i < 3; ++i)
    {
        gl.glActiveTexture(GL_TEXTURE0 + i);
        gl.glBindTexture(GL_TEXTURE_2D, textures[i]);
        program_.setUniformValue(names[i], i);
    }
    QMatrix4x4 view = camera.viewMatrix();
    QMatrix4x4 projection = camera.projectionMatrix(width_, height_);
    program_.setUniformValue("view", view);
    program_.setUniformValue("projection", projection);
    program_.setUniformValue("inverseProjection", projection.inverted());
    program_.setUniformValue("inverseView", view.inverted());
    program_.setUniformValue("useBloom",settings.bloom);
    program_.setUniformValue("useAO", settings.ambientOcclusion);
    program_.setUniformValue("useSSR", settings.screenSpaceReflections);
    program_.setUniformValue("useRays", settings.godRays);
    program_.setUniformValue("useFog", settings.volumetricFog);
    program_.setUniformValue("exposure", settings.exposure);
    program_.setUniformValue("fogDensity", settings.fogDensity);
    bool hybrid = settings.hardwareRayTracing && hybrid_.ready() && hybrid_.fullFrame() == settings.rayFullFrame;
    program_.setUniformValue("useHybrid", hybrid);
    program_.setUniformValue("fullRayFrame", hybrid && hybrid_.fullFrame());
    program_.setUniformValue("rayDenoise", settings.rayDenoise);
    program_.setUniformValue("rayStrength", settings.rayStrength);
    program_.setUniformValue("lightingDebug", settings.hardwareRayTracing ? settings.lightingDebug : 0);
    if (hybrid)
    {
        gl.glActiveTexture(GL_TEXTURE3);
        gl.glBindTexture(GL_TEXTURE_2D, hybrid_.lighting());
        gl.glActiveTexture(GL_TEXTURE4);
        gl.glBindTexture(GL_TEXTURE_2D, hybrid_.surface());
        program_.setUniformValue("rayLighting", 3);
        program_.setUniformValue("raySurface", 4);
        auto size = hybrid_.size();
        program_.setUniformValue("rayViewProjection", hybrid_.camera().projectionMatrix(size.width(), size.height()) * hybrid_.camera().viewMatrix());
        program_.setUniformValue("rayEye", hybrid_.camera().position);
    }
    gl.glBindVertexArray(vao_);
    gl.glDrawArrays(GL_TRIANGLES, 0, 3);
    gl.glBindVertexArray(0);
    gl.glActiveTexture(GL_TEXTURE0);
    program_.release();
    gl.glEnable(GL_DEPTH_TEST);
}
void PostProcessor::release(QOpenGLFunctions_3_3_Core& gl)
{
    releaseTargets(gl);
    hybrid_.release(gl);
    if (vao_) gl.glDeleteVertexArrays(1, &vao_);
    vao_ = 0;
    program_.removeAllShaders();
}
