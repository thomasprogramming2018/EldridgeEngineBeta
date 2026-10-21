#include "GPUMesh.h"
#include <limits>

bool GPUMesh::uploadToGPU(QOpenGLFunctions_3_3_Core& gl, const Mesh& mesh)
{
    if (mesh.vertices.empty() || mesh.indices.empty() ||
        mesh.vertices.size() % 8 != 0 ||
        mesh.indices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) return false;
    release(gl);
    gl.glGenVertexArrays(1, &vao_);
    gl.glBindVertexArray(vao_);
    gl.glGenBuffers(1, &vbo_);
    gl.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl.glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size()*sizeof(float)),
                    mesh.vertices.data(), GL_STATIC_DRAW);
    gl.glGenBuffers(1, &ebo_);
    gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size()*sizeof(unsigned int)),
                    mesh.indices.data(), GL_STATIC_DRAW);
    const GLsizei stride = 8*sizeof(float);
    gl.glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,nullptr);
    gl.glEnableVertexAttribArray(0);
    gl.glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<const void*>(3*sizeof(float)));
    gl.glEnableVertexAttribArray(1);
    gl.glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<const void*>(6*sizeof(float)));
    gl.glEnableVertexAttribArray(2);
    gl.glBindVertexArray(0);
    gl.glBindBuffer(GL_ARRAY_BUFFER, 0);
    indexCount_ = static_cast<GLsizei>(mesh.indices.size());
    return true;
}
void GPUMesh::draw(QOpenGLFunctions_3_3_Core& gl) const
{
    if (!vao_ || !indexCount_) return;
    gl.glBindVertexArray(vao_);
    gl.glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    gl.glBindVertexArray(0);
}
void GPUMesh::release(QOpenGLFunctions_3_3_Core& gl)
{
    if (ebo_) gl.glDeleteBuffers(1, &ebo_);
    if (vbo_) gl.glDeleteBuffers(1, &vbo_);
    if (vao_) gl.glDeleteVertexArrays(1, &vao_);
    vao_ = vbo_ = ebo_ = 0;
    indexCount_ = 0;
}
