#pragma once
#include "GPUMesh.h"
#include <QMatrix4x4>
#include <QOpenGLShaderProgram>
#include <QVector3D>
#include <vector>
struct ShadowDraw { const GPUMesh* mesh; QMatrix4x4 model; };
class ShadowMap
{
public:
    bool initialize(QOpenGLFunctions_3_3_Core& gl,QString& error);
    void render(QOpenGLFunctions_3_3_Core& gl,const std::vector<ShadowDraw>& draws,const QVector3D& minimum,const QVector3D& maximum);
    void bind(QOpenGLFunctions_3_3_Core& gl,int unit) const;
    const QMatrix4x4& matrix() const { return matrix_; }
    void release(QOpenGLFunctions_3_3_Core& gl);
private:
    GLuint framebuffer_=0,depth_=0;
    QOpenGLShaderProgram program_;
    QMatrix4x4 matrix_;
};
