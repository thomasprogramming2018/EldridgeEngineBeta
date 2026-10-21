#pragma once

#include <QString>

struct ShaderAsset
{
    QString name;
    QString vertexSource;
    QString fragmentSource;

    // Rendering settings live here. Add future settings beside this one.
    bool affectedByLights = true;

    bool load(const QString& path);
    bool save(const QString& path) const;
};