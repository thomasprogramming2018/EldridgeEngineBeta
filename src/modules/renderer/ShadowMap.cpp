#include "ShadowMap.h"
bool ShadowMap::initialize(QOpenGLFunctions_3_3_Core& gl,QString& error)
{
    const char* vertex=R"(#version 330 core
layout(location=0) in vec3 position;
uniform mat4 model;
uniform mat4 light;
void main(){gl_Position=light*model*vec4(position,1.0);})";
    const char* fragment="#version 330 core\nvoid main(){}";
    if(!program_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertex)||!program_.addShaderFromSourceCode(QOpenGLShader::Fragment,fragment)||!program_.link())
    {error=program_.log();return false;}
    gl.glGenTextures(1,&depth_);gl.glBindTexture(GL_TEXTURE_2D,depth_);
    gl.glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,2048,2048,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
    gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);
    const float border[]={1,1,1,1};gl.glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,border);
    GLint previous;gl.glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous);
    gl.glGenFramebuffers(1,&framebuffer_);gl.glBindFramebuffer(GL_FRAMEBUFFER,framebuffer_);
    gl.glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depth_,0);
    gl.glDrawBuffer(GL_NONE);gl.glReadBuffer(GL_NONE);
    bool complete=gl.glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    gl.glBindFramebuffer(GL_FRAMEBUFFER,previous);
    if(!complete) error="Shadow framebuffer is incomplete";
    return complete;
}
void ShadowMap::render(QOpenGLFunctions_3_3_Core& gl,const std::vector<ShadowDraw>& draws,const QVector3D& minimum,const QVector3D& maximum)
{
    QVector3D center=(minimum+maximum)*0.5f;
    float radius=std::max((maximum-minimum).length()*0.55f,4.f);
    QMatrix4x4 view;view.lookAt(center+QVector3D(0.4f,1.f,0.3f).normalized()*radius*2,center,QVector3D(0,0,1));
    QMatrix4x4 projection;projection.ortho(-radius,radius,-radius,radius,0.1f,radius*5.f);
    matrix_=projection*view;
    GLint previous,viewport[4];gl.glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous);gl.glGetIntegerv(GL_VIEWPORT,viewport);
    gl.glBindFramebuffer(GL_FRAMEBUFFER,framebuffer_);gl.glViewport(0,0,2048,2048);gl.glEnable(GL_DEPTH_TEST);gl.glClear(GL_DEPTH_BUFFER_BIT);
    gl.glEnable(GL_POLYGON_OFFSET_FILL);gl.glPolygonOffset(1.f,2.f);
    program_.bind();program_.setUniformValue("light",matrix_);
    for(const auto& draw:draws) {program_.setUniformValue("model",draw.model);draw.mesh->draw(gl);}
    program_.release();gl.glDisable(GL_POLYGON_OFFSET_FILL);
    gl.glBindFramebuffer(GL_FRAMEBUFFER,previous);gl.glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
}
void ShadowMap::bind(QOpenGLFunctions_3_3_Core& gl,int unit) const
{gl.glActiveTexture(GL_TEXTURE0+unit);gl.glBindTexture(GL_TEXTURE_2D,depth_);}
void ShadowMap::release(QOpenGLFunctions_3_3_Core& gl)
{
    if(depth_) gl.glDeleteTextures(1,&depth_);if(framebuffer_) gl.glDeleteFramebuffers(1,&framebuffer_);
    depth_=framebuffer_=0;program_.removeAllShaders();
}
