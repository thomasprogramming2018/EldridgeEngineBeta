#include "LightComponent.h"
#include "../../scene/Scene.h"
#include "../TransformComponent/TransformComponent.h"
#include <QJsonArray>
#include <cmath>
#include <stdexcept>

LightData LightComponent::worldLight() const
{
    LightData result;
    result.type = int(type); result.color = color; result.intensity = intensity;
    result.range = range; result.radius = radius; result.angle = angle;
    result.innerCone = innerCone; result.outerCone = outerCone;
    result.width = width; result.height = height; result.shadows = shadows;
    if (auto* transform = owner_.getComponent<TransformComponent>())
    {
        auto model = transform->getModelMatrix();
        result.position = model.map(QVector3D());
        result.direction = model.mapVector({0, 0, -1}).normalized();
        result.right = model.mapVector({1, 0, 0}).normalized();
        if(result.direction.lengthSquared()<.001f)result.direction={0,0,-1};
        // Keep the emitter frame orthogonal even with a scaled parent.
        result.right=(result.right-result.direction*QVector3D::dotProduct(result.right,result.direction)).normalized();
        if(result.right.lengthSquared()<.001f)
            result.right=QVector3D::crossProduct(result.direction,std::abs(result.direction.y())<.9f?QVector3D(0,1,0):QVector3D(1,0,0)).normalized();
        result.up = QVector3D::crossProduct(result.right, result.direction).normalized();
        result.width *= model.mapVector({1, 0, 0}).length();
        result.height *= model.mapVector({0, 1, 0}).length();
    }
    return result;
}
QJsonObject LightComponent::toJson() const
{
    return {{"type", int(type)}, {"enabled", enabled}, {"shadows", shadows},
            {"color", QJsonArray{color.x(), color.y(), color.z()}}, {"intensity", intensity},
            {"range", range}, {"radius", radius}, {"angle", angle}, {"innerCone", innerCone},
            {"outerCone", outerCone}, {"width", width}, {"height", height}};
}
bool LightComponent::fromJson(const QJsonObject& data, QString& error)
{
    try
    {
        auto number = [&](const char* name, float low, float high)
        {
            auto value = data.value(name);
            double result = value.toDouble(-1e30);
            if (!value.isDouble() || !std::isfinite(result) || result < low || result > high)
                throw std::runtime_error("Invalid light field: " + std::string(name));
            return float(result);
        };
        float kind = number("type", 0, 3);
        if (kind != std::floor(kind) || !data.value("enabled").isBool() || !data.value("shadows").isBool())
            throw std::runtime_error("Invalid light type or flags");
        auto rgb = data.value("color").toArray();
        if (rgb.size() != 3) throw std::runtime_error("Invalid light color");
        QVector3D tint;
        for (int i = 0; i < 3; ++i)
        {
            double value = rgb[i].toDouble(-1);
            if (!rgb[i].isDouble() || !std::isfinite(value) || value < 0 || value > 1)
                throw std::runtime_error("Invalid light color");
            tint[i] = value;
        }
        type = Type(int(kind)); enabled = data["enabled"].toBool(); shadows = data["shadows"].toBool(); color = tint;
        intensity = number("intensity", 0, 100000); range = number("range", 0.01f, 100000);
        radius = number("radius", 0, 1000); angle = number("angle", 0, 20);
        innerCone = number("innerCone", 0, 89); outerCone = number("outerCone", 0.1f, 89.9f);
        if (innerCone > outerCone) throw std::runtime_error("Inner light cone exceeds outer cone");
        width = number("width", 0.01f, 10000); height = number("height", 0.01f, 10000);
        error.clear(); return true;
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}
std::vector<LightData> collectLights(const Scene& scene)
{
    std::vector<LightData> lights;
    for (const auto& object : scene.getObjects())
        if (auto* light = object->getComponent<LightComponent>(); light && light->enabled && light->intensity > 0)
            lights.push_back(light->worldLight());
    return lights;
}
bool hasSceneLights(const Scene& scene)
{
    for (const auto& object : scene.getObjects()) if (object->getComponent<LightComponent>()) return true;
    return false;
}
