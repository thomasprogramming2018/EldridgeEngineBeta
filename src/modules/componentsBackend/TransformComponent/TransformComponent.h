#pragma once

#include "../component.h"
#include <QMatrix4x4>
#include <QVector3D>

class TransformComponent : public Component
{
public:
    explicit TransformComponent(SceneObject& owner) : Component(owner) {}

    QVector3D position {0, 0, 0};
    QVector3D rotation {0, 0, 0}; // Euler angles in degrees.
    QVector3D scale {1, 1, 1};

    QMatrix4x4 getModelMatrix() const;
};
