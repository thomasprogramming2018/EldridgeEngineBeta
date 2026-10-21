#include "SceneObject.h"
#include "../componentsBackend/MeshComponent/MeshComponent.h"

#include "../componentsBackend/TransformComponent/TransformComponent.h"
#include <algorithm>

SceneObject::SceneObject(std::string name) : name_(std::move(name))
{
    addComponent<TransformComponent>();
}

SceneObject::~SceneObject()
{
    setParent(nullptr);
    for (auto* child : children_)
        child->parent_ = nullptr;
}

bool SceneObject::setParent(SceneObject* parent)
{
    // Reject cycles, including parenting an object to itself.
    for (auto* object = parent; object; object = object->parent_)
        if (object == this)
            return false;

    if (parent_ == parent)
        return true;

    if (parent_)
    {
        auto& siblings = parent_->children_;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
    }

    parent_ = parent;
    if (parent_)
        parent_->children_.push_back(this);
    return true;
}

void SceneObject::createCube()
{
    auto* component = getComponent<MeshComponent>();
    if (!component)
        component = &addComponent<MeshComponent>();
    component->createCube();
}
