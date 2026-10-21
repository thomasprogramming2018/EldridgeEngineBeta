#include "modules/scene/Scene.h"
#include "modules/scene/SceneManager.h"
#include "modules/componentsBackend/MeshComponent/MeshComponent.h"
#include "modules/componentsBackend/TransformComponent/TransformComponent.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

void require(bool condition)
{
    if (!condition) throw std::runtime_error("Mesh creation check failed");
}

int main(int argc, char** argv)
{
    try {
        require(argc == 2);
        const std::filesystem::path directory = argv[1];
        std::filesystem::create_directories(directory);
        const auto path = directory / "quad.obj";
        {
            std::ofstream file(path);
            file << "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
                    "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                    "f -4/1 -3/2 -2/3 -1/4\n";
        }
        SceneObject parent("Parent");
        SceneObject child("Child");
        SceneObject grandchild("Grandchild");
        auto* parentTransform = parent.getComponent<TransformComponent>();
        auto* childTransform = child.getComponent<TransformComponent>();
        auto* grandchildTransform = grandchild.getComponent<TransformComponent>();
        require(parentTransform && childTransform && grandchildTransform);
        require(childTransform->getModelMatrix().isIdentity());
        parentTransform->position = {10, 0, 0};
        parentTransform->rotation = {0, 0, 90};
        parentTransform->scale = {2, 2, 2};
        childTransform->position = {1, 0, 0};
        require(child.setParent(&parent));
        require(grandchild.setParent(&child));
        grandchildTransform->position = {1, 0, 0};
        require((childTransform->getModelMatrix().map(QVector3D()) - QVector3D(10, 2, 0)).length() < 0.0001f);
        require((grandchildTransform->getModelMatrix().map(QVector3D()) - QVector3D(10, 4, 0)).length() < 0.0001f);
        require(!parent.setParent(&grandchild));
        require(!child.setParent(&child));
        require(child.setParent(&parent) && parent.children().size() == 1);
        require(child.setParent(nullptr) && parent.children().empty());
        require((childTransform->getModelMatrix().map(QVector3D()) - QVector3D(1, 0, 0)).length() < 0.0001f);
        require((childTransform->getModelMatrix().map(QVector3D()) - QVector3D(1, 0, 0)).length() < 0.0001f);
        {
            SceneObject temporary;
            require(child.setParent(&temporary));
        }
        require(child.parent() == nullptr);
        {
            SceneObject temporary;
            require(temporary.setParent(&parent));
        }
        require(parent.children().empty());

        SceneManager scenes;
        auto& firstScene = scenes.createScene("First");
        auto& secondScene = scenes.createScene("Second");
        require(firstScene.name() == "First" && secondScene.name() == "Second");
        require(scenes.activeScene == &firstScene);
        firstScene.createObject("Only First");
        scenes.SetActiveScene(secondScene);
        require(scenes.activeScene == &secondScene);
        require(secondScene.getObjects().empty());
        scenes.activeScene->createObject("Only Second");
        scenes.SetActiveScene(firstScene);
        require(scenes.activeScene->getObjects().size() == 1);
        require(scenes.activeScene->getObjects()[0]->name() == "Only First");
        require(secondScene.getObjects()[0]->name() == "Only Second");

        Scene scene;
        auto& cube = scene.createCube();
        auto* component = cube.getComponent<MeshComponent>();
        require(component && component->source() == MeshComponent::Source::Cube);
        require(component->mesh->vertices.size() == 24 * 8);
        require(component->mesh->indices.size() == 36);
        cube.createCube();
        require(component == cube.getComponent<MeshComponent>());
        auto& model = scene.importModel(path);
        auto* imported = model.getComponent<MeshComponent>();
        require(model.name() == "quad");
        require(imported && imported->source() == MeshComponent::Source::Imported);
        require(!imported->assetPath().empty());
        require(imported->mesh->vertices.size() == 6 * 8);
        require(imported->mesh->vertices[5] == 1.0f);
        require(imported->mesh->vertices[14] == 1.0f);
        const auto count = scene.getObjects().size();
        const auto bad = directory / "bad.obj";
        { std::ofstream file(bad); file << "v 0 0 0\nf 1 2 3\n"; }
        bool rejected = false;
        try { scene.importModel(bad); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected && scene.getObjects().size() == count);
        auto* previous = imported->mesh.get();
        rejected = false;
        try { imported->importMesh(bad); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected && imported->mesh.get() == previous);
        { std::ofstream file(path); file << "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
            "vn 0 0 2\nf 1//1 2//1 3//1\n"; }
        imported->importMesh(path);
        require(imported->mesh->vertices[5] == 1.0f && imported->mesh->vertices[6] == 0.0f);
        model.createCube();
        require(imported->source() == MeshComponent::Source::Cube && imported->assetPath().empty());
        std::filesystem::remove(path);
        std::filesystem::remove(bad);
        std::cout << "Mesh creation checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
