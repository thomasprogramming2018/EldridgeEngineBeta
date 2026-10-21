#pragma once

#include <memory>

#include "../scene/Scene.h"

#include "../material/MaterialComponent.h"


class SceneObject;
class Material;

class ObjectFactory
{
public:
    explicit ObjectFactory(Scene& scene);

    SceneObject& createEmpty();

    SceneObject& createCube(
        std::shared_ptr<Material> defaultMaterial
    );

    
    void setScene(Scene& scene);

private:
    Scene* scene_;
};