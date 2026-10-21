#pragma once
#include "../component.h"
#include <QJsonObject>
#include <QVector3D>
#include <vector>
class Scene;

// Engine lighting units. Direction points along the emitted rays (local -Z).
struct LightData
{
    int type = 1; // Directional, point, spot, rectangle.
    QVector3D position, direction {0, 0, -1}, right {1, 0, 0}, up {0, 1, 0};
    QVector3D color {1, 1, 1};
    float intensity = 30, range = 20, radius = 0.1f, angle = 0.53f;
    float innerCone = 25, outerCone = 40, width = 2, height = 2;
    bool shadows = true;
};
class LightComponent : public Component
{
public:
    enum class Type { Directional, Point, Spot, Rectangle };
    explicit LightComponent(SceneObject& owner) : Component(owner) {}
    Type type = Type::Point;
    bool enabled = true, shadows = true;
    QVector3D color {1, 1, 1};
    float intensity = 30, range = 20, radius = 0.1f, angle = 0.53f;
    float innerCone = 25, outerCone = 40, width = 2, height = 2;
    LightData worldLight() const;
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& data, QString& error);
};
std::vector<LightData> collectLights(const Scene& scene);
bool hasSceneLights(const Scene& scene);
