#pragma once
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions_3_3_Core>
#include <QHash>
#include <QString>
#include <memory>
#include <vector>

#include "ShaderParameter.h"
#include "ShaderAsset.h"
struct ShaderFile {
    QOpenGLShader::ShaderTypeBit stage;
    QString path;
    QString source;
};
struct ShaderDescription {
    std::vector<ShaderFile> files;
    QHash<QString, EditorHint> editorHints;
    bool affectedByLights = true;
};
struct ReflectedUniform {
    QString name;
    GLenum type = 0;
    GLint location = -1;
    GLint arraySize = 1;
};

// GPU program and CPU-side reflection data. Load/destroy with a current GL context.
class Shader {
public:
    bool load(const ShaderDescription& description);
    bool loadAsset(const ShaderAsset& asset, const QString& path);
    bool affectedByLights() const { return affectedByLights_; }
    const QString& assetPath() const { return assetPath_; }
    int uniformLocation(const QString& name) const;
    GLuint programId() const { return program_ ? program_->programId() : 0; }
    bool bind() const;
    void release();
    bool loadSource(
        const QString& vertexSource,
        const QString& fragmentSource
    );
    const QString& log() const { return log_; }
    const std::vector<ReflectedUniform>& uniforms() const { return uniforms_; }
    const ReflectedUniform* findUniform(const QString& name) const;
    EditorHint editorHint(const QString& name) const;
private:
    std::unique_ptr<QOpenGLShaderProgram> program_;
    std::vector<ReflectedUniform> uniforms_;
    QString log_;
    QString assetPath_;
    bool affectedByLights_ = true;
    QHash<QString, EditorHint> hints_;
};
