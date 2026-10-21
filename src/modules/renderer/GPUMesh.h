#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include "../componentsBackend/MeshComponent/Mesh.h"

class GPUMesh {
public:
    GPUMesh() = default;
    GPUMesh(const GPUMesh&) = delete;
    GPUMesh& operator=(const GPUMesh&) = delete;
    bool uploadToGPU(QOpenGLFunctions_3_3_Core& gl, const Mesh& mesh);
    void draw(QOpenGLFunctions_3_3_Core& gl) const;
    void release(QOpenGLFunctions_3_3_Core& gl);
private:
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    GLsizei indexCount_ = 0;
};
