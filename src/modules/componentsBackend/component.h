#pragma once

class SceneObject;

class Component
{
public:
    explicit Component(SceneObject& owner)
        : owner_(owner) {}

    virtual ~Component() = default;

protected:
    SceneObject& owner_;
};