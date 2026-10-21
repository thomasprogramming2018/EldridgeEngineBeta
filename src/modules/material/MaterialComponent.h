#pragma once
#include "../componentsBackend/component.h"
#include "Material.h"
class MaterialComponent : public Component {
public:
    explicit MaterialComponent(SceneObject& owner) : Component(owner) {}
    std::shared_ptr<Material> material;
};
