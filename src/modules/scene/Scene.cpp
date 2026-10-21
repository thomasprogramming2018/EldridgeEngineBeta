#include "Scene.h"
#include <algorithm>

void Scene::initialize()
{
}

void Scene::render()
{
}

bool Scene::removeObject(SceneObject* object)
{
    auto it = std::find_if(
        objects_.begin(),
        objects_.end(),
        [object](const auto& owned)
        {
            return owned.get() == object;
        }
    );

    if (it == objects_.end())
        return false;

    objects_.erase(it);

    emit sceneChanged();
    
    return true;
}



#include "../componentsBackend/MeshComponent/MeshComponent.h"

SceneObject& Scene::createObject(std::string name)
{
    auto object = std::make_unique<SceneObject>(std::move(name));
    auto& result = *object;
    objects_.push_back(std::move(object));

    emit sceneChanged();

    return result;
}

SceneObject& Scene::createCube(std::string name)
{
    auto object = std::make_unique<SceneObject>(std::move(name));
    object->createCube();
    auto& result = *object;
    objects_.push_back(std::move(object));

    emit sceneChanged();


    return result;
}

SceneObject& Scene::importModel(const std::filesystem::path& path)
{
    auto object = std::make_unique<SceneObject>(path.stem().string());
    object->addComponent<MeshComponent>().importMesh(path);
    auto& result = *object;
    objects_.push_back(std::move(object));

    emit sceneChanged();



    return result;
}

void Scene::appendObjects(Scene& source)
{
    for (auto& object : source.objects_) objects_.push_back(std::move(object));
    source.objects_.clear();
    emit sceneChanged();
}

void Scene::markSaved()
{
    savedState_ = toJson();
    emit historyChanged();
}

#include "../componentsBackend/TerrainComponent/TerrainComponent.h"
#include "../material/MaterialComponent.h"
SceneObject& Scene::createTerrain(std::string name)
{
    auto object = std::make_unique<SceneObject>(std::move(name));
    auto& terrain = object->addComponent<TerrainComponent>();
    terrain.resize(129, 64.f);
    terrain.generate(1, 6.f, 4.f);
    auto& material = object->addComponent<MaterialComponent>();
    material.material = std::make_shared<Material>();
    material.material->parameters["material.roughness"] = 0.8f;
    material.material->parameters["material.metallic"] = 0.f;
#ifdef ENGINE_SHADER_DIRECTORY
    material.material->shaderPath = QString(ENGINE_SHADER_DIRECTORY) + "/Terrain.elsha";
#endif
    auto& result = *object;
    objects_.push_back(std::move(object));
    emit sceneChanged();
    return result;
}

#include "../componentsBackend/LightComponent/LightComponent.h"
SceneObject& Scene::createLight(int type, std::string name)
{
    auto object = std::make_unique<SceneObject>(std::move(name));
    auto& light = object->addComponent<LightComponent>();
    light.type = LightComponent::Type(std::clamp(type, 0, 3));
    if (light.type == LightComponent::Type::Directional) light.intensity = 3;
    auto& result = *object;
    objects_.push_back(std::move(object));
    emit sceneChanged();
    return result;
}
