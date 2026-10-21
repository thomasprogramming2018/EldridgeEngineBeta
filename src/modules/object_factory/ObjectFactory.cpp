#include "ObjectFactory.h"



ObjectFactory::ObjectFactory(Scene& scene)
        : scene_(&scene)
{

}


SceneObject& ObjectFactory::createCube(
    std::shared_ptr<Material> defaultMaterial)
{
    SceneObject& cube = scene_->createCube("Cube");

    auto* component = cube.getComponent<MaterialComponent>();

    if (!component)
        component = &cube.addComponent<MaterialComponent>();

    if (defaultMaterial)
    {
        component->material =
            std::make_shared<Material>(*defaultMaterial);
    }

    return cube;
}

SceneObject& ObjectFactory::createEmpty()
{
    SceneObject& empty = scene_->createObject("empty");
    
    return empty;
}






void ObjectFactory::setScene(Scene& scene)
{
    scene_ = &scene;
}