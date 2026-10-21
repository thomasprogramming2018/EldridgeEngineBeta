#pragma once

#include "../componentsBackend/component.h"
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class SceneObject
{
public:
    explicit SceneObject(std::string name = "Object");
    ~SceneObject();
    SceneObject(const SceneObject&) = delete;
    SceneObject& operator=(const SceneObject&) = delete;

    const std::string& name() const { return name_; }

    template<class T, class... Args>
    T& addComponent(Args&&... args)
    {
        static_assert(std::is_base_of_v<Component, T>);
        auto component = std::make_unique<T>(*this, std::forward<Args>(args)...);
        auto& result = *component;
        components_.push_back(std::move(component));
        return result;
    }

    template<class T>
    T* getComponent() const
    {
        for (const auto& component : components_)
            if (auto* result = dynamic_cast<T*>(component.get()))
                return result;
        return nullptr;
    }

    const auto& components() const
    {
        return components_;
    }

    
    void setName(const std::string& name)
    {
        name_ = name;
    }

    SceneObject* parent() const { return parent_; }
    const auto& children() const { return children_; }
    bool setParent(SceneObject* parent);

    void createCube();
    
private:
    SceneObject* parent_ = nullptr;
    std::vector<SceneObject*> children_;
    std::string name_;
    std::vector<std::unique_ptr<Component>> components_;
};
