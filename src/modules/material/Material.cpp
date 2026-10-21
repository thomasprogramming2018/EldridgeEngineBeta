#include "Material.h"

bool Material::isEngineUniform(const QString& name)
{
    return name == "uModel" || name == "uView" || name == "uProjection" ||
           name == "uCameraPosition" || name == "uLightDirection" || name == "uLightViewProjection" || name.startsWith("uShadow") ||
           name == "uTime" || name == "uAffectedByLights" || name.startsWith("uTerrain") ||
           name == "camera.Pos" || name == "lightCount" ||
           name == "uSceneLights" || name == "uLightCount" || name.startsWith("lights[") ||
           name.startsWith("uLights[") || name.startsWith("dlight.");
}
bool Material::isEditableUniform(const ReflectedUniform& u)
{
    if (u.location < 0 || u.arraySize != 1 || isEngineUniform(u.name)) return false;
    switch (u.type) {
    case GL_FLOAT: case GL_INT: case GL_BOOL:
    case GL_FLOAT_VEC2: case GL_FLOAT_VEC3: case GL_FLOAT_VEC4:
    case GL_FLOAT_MAT4: case GL_SAMPLER_2D: return true;
    default: return false;
    }
}
MaterialValue Material::defaultValue(GLenum type)
{
    switch (type) {
    case GL_INT: return 1;
    case GL_BOOL: return false;
    case GL_FLOAT_VEC2: return QVector2D(1, 1);
    case GL_FLOAT_VEC3: return QVector3D(0.82f, 0.52f, 0.34f);
    case GL_FLOAT_VEC4: return QVector4D(1, 1, 1, 1);
    case GL_FLOAT_MAT4: return QMatrix4x4{};
    case GL_SAMPLER_2D: return TexturePath{};
    default: return 0.5f;
    }
}
void Material::setShader(std::shared_ptr<Shader> newShader)
{
    QHash<QString, MaterialValue> updated;
    if (newShader) {
        for (const auto& uniform : newShader->uniforms()) {
            if (!isEditableUniform(uniform)) continue;
            const auto previous = parameters.constFind(uniform.name);
            MaterialValue fallback = defaultValue(uniform.type);
            if (uniform.name == "material.metallic") fallback = 0.f;
            if (uniform.name == "material.roughness") fallback = 0.45f;
            if (uniform.name == "material.emissive") fallback = QVector3D(0, 0, 0);
            if (uniform.name == "material.specular_color") fallback = QVector3D(0.04f, 0.04f, 0.04f);
            if (uniform.name == "material.normal_vector") fallback = QVector3D(0, 0, 1);
            if (uniform.name == "material.normalStrength") fallback = 1.f;
            if (uniform.name.startsWith("material.has")) fallback = 0;
            if (uniform.name == "material.clearCoat") fallback = 1.f;
            if (uniform.name == "material.clearCoatRoughness") fallback = 0.15f;
            // Variant indices correspond to the compatible GLSL uniform types.
            if (previous != parameters.cend() && previous.value().index() == fallback.index())
                updated.insert(uniform.name, previous.value());
            else
                updated.insert(uniform.name, fallback);
        }
    }
    if (newShader)
        shaderPath = newShader->assetPath();
    shader = std::move(newShader);
    parameters = std::move(updated);
}
const MaterialValue* Material::get(const QString& uniform) const
{
    const auto it = parameters.constFind(uniform);
    return it == parameters.cend() ? nullptr : &it.value();
}
void Material::set(const QString& uniform, const MaterialValue& value)
{
    if (parameters.contains(uniform) && parameters.value(uniform).index() == value.index())
        parameters.insert(uniform, value);
}
