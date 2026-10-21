#pragma once

#include <QMatrix4x4>
#include <QVector3D>
#include <QtMath>
#include <algorithm>

class Camera
{
public:
    QVector3D position {3.0f, 2.0f, 4.0f};
    float yaw_ = -127.f;
    float pitch_ = -20.f;
    float nearPlane = 0.05f;
    float farPlane = 500.f;

    QVector3D forward() const
    {
        const float yaw = qDegreesToRadians(yaw_);
        const float pitch = qDegreesToRadians(pitch_);
        return {qCos(pitch) * qCos(yaw), qSin(pitch), qCos(pitch) * qSin(yaw)};
    }

    QMatrix4x4 viewMatrix() const
    {
        QMatrix4x4 view;
        view.lookAt(position, position + forward(), QVector3D(0, 1, 0));
        return view;
    }

    QMatrix4x4 projectionMatrix(int width, int height) const
    {
        QMatrix4x4 projection;
        projection.perspective(55.f, float(std::max(1, width)) / float(std::max(1, height)),
                               std::max(nearPlane, 0.001f), std::max(farPlane, nearPlane + 1.f));
        return projection;
    }
};
