#pragma once
#include "Scene.h"
#include <memory>
#include <vector>
#include <QString>


class SceneManager

{
public:

    // The manager owns the scene; the reference lives as long as the manager.
    Scene& createScene(QString name)
    {
        auto scene = std::make_unique<Scene>();
        scene->setName(name);
        scene->resetHistory();
        auto& result = *scene;
        scenes_.push_back(std::move(scene));
        if (!activeScene)
            activeScene = &result;
        return result;
    }

    
    void SetActiveScene(Scene& scene)
    {
        activeScene = &scene;
    }

    Scene& addScene(std::unique_ptr<Scene> scene)
    {
        auto& result = *scene;
        scenes_.push_back(std::move(scene));
        if (!activeScene) activeScene = &result;
        return result;
    }

    void replaceScenes(std::vector<std::unique_ptr<Scene>> scenes, int active)
    {
        scenes_ = std::move(scenes);
        activeScene = scenes_[active].get();
    }

    const auto& scenes() const { return scenes_; }

    Scene* activeScene = nullptr;


private:
    std::vector<std::unique_ptr<Scene>> scenes_;
};
