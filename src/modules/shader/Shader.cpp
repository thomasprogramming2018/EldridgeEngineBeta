#include "Shader.h"
#include <QOpenGLContext>
#include <QDebug>
#include <utility>
#include <algorithm>

bool Shader::load(const ShaderDescription& description)
{
    if (!QOpenGLContext::currentContext()) {
        log_ = "Shader loading requires a current OpenGL context";
        return false;
    }
    if (description.files.empty()) {
        log_ = "Shader has no stages";
        return false;
    }
    QOpenGLFunctions_3_3_Core gl;
    if (!gl.initializeOpenGLFunctions()) {
        log_ = "OpenGL 3.3 functions unavailable";
        return false;
    }
    auto candidate = std::make_unique<QOpenGLShaderProgram>();
    for (const auto& file : description.files) {
        bool compiled;
        if (file.source.isEmpty())
            compiled = candidate->addShaderFromSourceFile(file.stage, file.path);
        else
            compiled = candidate->addShaderFromSourceCode(file.stage, file.source);
        if (!compiled) {
            log_ = QString("Failed compiling %1: %2").arg(file.path, candidate->log());
            return false;
        }
    }
    if (!candidate->link()) {
        log_ = QString("Program link failed: %1").arg(candidate->log());
        return false;
    }

    const GLuint id = candidate->programId();
    GLint count = 0, maxNameLength = 0;
    gl.glGetProgramiv(id, GL_ACTIVE_UNIFORMS, &count);
    gl.glGetProgramiv(id, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxNameLength);
    std::vector<GLchar> buffer(static_cast<std::size_t>(std::max(1, maxNameLength)));
    std::vector<ReflectedUniform> discovered;
    for (GLint i = 0; i < count; ++i) {
        GLsizei length = 0;
        GLint arraySize = 0;
        GLenum type = 0;
        gl.glGetActiveUniform(id, static_cast<GLuint>(i), static_cast<GLsizei>(buffer.size()),
                              &length, &arraySize, &type, buffer.data());
        if (length <= 0) continue;
        const QString name = QString::fromLatin1(buffer.data(), length);
        const GLint location = gl.glGetUniformLocation(id, buffer.data());
        // Expand basic GLSL arrays into one editable entry per element.
        if (arraySize > 1 && name.endsWith("[0]")) {
            const QString base = name.left(name.size() - 3);
            for (GLint j = 0; j < arraySize; ++j) {
                const QString element = base + "[" + QString::number(j) + "]";
                const QByteArray encoded = element.toLatin1();
                discovered.push_back({element, type, gl.glGetUniformLocation(id, encoded.constData()), 1});
            }
        } else {
            discovered.push_back({name, type, location, arraySize});
        }
        qInfo() << "Reflected:" << name << "GL type:" << type << "location:" << location;
    }
    // Commit only after the candidate program and its reflection both succeed.
    program_ = std::move(candidate);
    uniforms_ = std::move(discovered);
    hints_ = description.editorHints;
    affectedByLights_ = description.affectedByLights;
    log_ = "Shader compiled and linked successfully";
    return true;
}

bool Shader::bind() const { return program_ && program_->bind(); }
void Shader::release() { uniforms_.clear(); program_.reset(); }
const ReflectedUniform* Shader::findUniform(const QString& name) const
{
    for (const auto& uniform : uniforms_)
        if (uniform.name == name) return &uniform;
    return nullptr;
}

EditorHint Shader::editorHint(const QString& name) const { return hints_.value(name, EditorHint::Auto); }

bool Shader::loadSource(const QString& vertexSource, const QString& fragmentSource)
{
    ShaderDescription description;
    description.files = {
        {QOpenGLShader::Vertex, "Vertex source", vertexSource},
        {QOpenGLShader::Fragment, "Fragment source", fragmentSource}
    };
    return load(description);
}

bool Shader::loadAsset(const ShaderAsset& asset, const QString& path)
{
    if (asset.vertexSource.trimmed().isEmpty() || asset.fragmentSource.trimmed().isEmpty())
    {
        log_ = "Both vertex and fragment source are required";
        return false;
    }
    ShaderDescription description;
    description.files = {
        {QOpenGLShader::Vertex, "Vertex source", asset.vertexSource},
        {QOpenGLShader::Fragment, "Fragment source", asset.fragmentSource}
    };
    description.affectedByLights = asset.affectedByLights;
    description.editorHints.insert("uBaseColor", EditorHint::Color);
    description.editorHints.insert("uTintColor", EditorHint::Color);
    description.editorHints.insert("uRoughness", EditorHint::Slider);
    if (!load(description))
        return false;
    assetPath_ = path;
    return true;
}

int Shader::uniformLocation(const QString& name) const
{
    return program_ ? program_->uniformLocation(name) : -1;
}
