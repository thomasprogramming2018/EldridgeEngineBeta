#pragma once
#include "../scene/SceneManager.h"
#include <QJsonObject>

// All file operations are CPU-only. Loading is staged before the editor changes.
class ProjectFile
{
public:
    static bool save(const QString& path, const SceneManager& scenes,
                     const QJsonObject& graphics, QString& error);
    static bool load(const QString& path, std::vector<std::unique_ptr<Scene>>& scenes,
                     int& activeScene, QJsonObject& graphics, QString& error);
    static bool savePrefab(const QString& path, const Scene& scene, SceneObject& root, QString& error);
    static bool loadPrefab(const QString& path, Scene& scene, QString& error);
};
