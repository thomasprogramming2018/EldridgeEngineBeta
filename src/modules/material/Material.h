#pragma once
#include <QHash>
#include <QString>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QMatrix4x4>
#include <memory>
#include <variant>
#include "../shader/Shader.h"

struct TexturePath { QString path; }; // CPU-side descriptor, not a GPU texture
using MaterialValue = std::variant<float, int, bool, QVector2D, QVector3D, QVector4D, QMatrix4x4, TexturePath>;

class Material {
public:
    QString name = "Default Material";
    QString shaderPath; // Also available before the GPU shader has been loaded.
    std::shared_ptr<Shader> shader;
    QHash<QString, MaterialValue> parameters;

    // Keep matching values when changing programs; supply defaults for new uniforms.
    void setShader(std::shared_ptr<Shader> newShader);
    const MaterialValue* get(const QString& uniform) const;
    void set(const QString& uniform, const MaterialValue& value);
    static bool isEngineUniform(const QString& name);
    static bool isEditableUniform(const ReflectedUniform& uniform);
    static MaterialValue defaultValue(GLenum type);
};
