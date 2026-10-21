#include "modules/scene/Scene.h"
#include "modules/componentsBackend/TransformComponent/TransformComponent.h"
#include "modules/componentsBackend/MeshComponent/MeshComponent.h"
#include "modules/material/MaterialComponent.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        Scene scene;
        scene.setName("Saved Scene");
        scene.resetHistory();
        require(!scene.canUndo() && !scene.isModified(), "Initial history");
        auto& parent = scene.createCube("Parent");
        auto& child = scene.createObject("Child");
        child.setParent(&parent);
        parent.getComponent<TransformComponent>()->position = {2, 3, 4};
        child.getComponent<TransformComponent>()->rotation = {10, 20, 30};
        child.getComponent<TransformComponent>()->scale = {2, 4, -1};
        auto& material = parent.addComponent<MaterialComponent>();
        material.material = std::make_shared<Material>();
        material.material->name = "Test Material";
        material.material->shaderPath = "/tmp/Example.elsha";
        auto& parameters = material.material->parameters;
        parameters["scalar"] = 0.25f;
        parameters["integer"] = 7;
        parameters["toggle"] = true;
        parameters["uv"] = QVector2D(2, 3);
        parameters["color"] = QVector3D(0.2f, 0.4f, 0.6f);
        parameters["tint"] = QVector4D(1, 2, 3, 4);
        QMatrix4x4 matrix;
        matrix.translate(4, 5, 6);
        parameters["matrix"] = matrix;
        parameters["texture"] = TexturePath{"/tmp/texture.png"};
        scene.recordEdit();
        auto full = scene.toJson();
        require(scene.canUndo() && scene.isModified(), "Record edit");
        require(scene.undo() && scene.getObjects().empty(), "Undo creation");
        require(scene.redo() && scene.toJson() == full, "Redo full scene");
        auto* restoredParent = scene.getObjects()[0].get();
        auto* restoredChild = scene.getObjects()[1].get();
        require(restoredChild->parent() == restoredParent, "Restore parent relationship");
        require(restoredParent->getComponent<MeshComponent>()->mesh->indices.size() == 36, "Restore mesh geometry");
        auto* restoredMaterial = restoredParent->getComponent<MaterialComponent>()->material.get();
        require(restoredMaterial->parameters.size() == 8, "Round trip all uniform types");
        require(std::get<QMatrix4x4>(restoredMaterial->parameters["matrix"]) == matrix, "Restore matrix values");
        require(scene.removeObject(restoredParent), "Delete parent");
        scene.recordEdit();
        require(scene.getObjects()[0]->parent() == nullptr, "Deleting parent detaches child");
        require(scene.undo() && scene.toJson() == full, "Undo deletion");
        scene.getObjects()[1]->setName("Renamed");
        scene.recordEdit();
        require(!scene.canRedo(), "New edit discards redo branch");

        QTemporaryDir directory;
        QString path = directory.filePath("Example.elscene");
        QString error;
        require(scene.save(path, error) && !scene.isModified(), "Atomic save and clean state");
        Scene loaded;
        require(loaded.load(path, error), "Load saved scene");
        require(loaded.toJson() == scene.toJson(), "Saved scene matches loaded scene");
        require(!loaded.canUndo() && !loaded.isModified(), "Loaded scene starts clean");
        auto valid = loaded.toJson();
        auto broken = valid;
        auto objects = broken["objects"].toArray();
        auto first = objects[0].toObject();
        first["parent"] = 1; // Existing child already points to this parent.
        objects[0] = first;
        broken["objects"] = objects;
        require(!loaded.fromJson(broken, error) && loaded.toJson() == valid, "Reject parenting cycle without changing scene");
        objects = valid["objects"].toArray();
        first = objects[0].toObject();
        first["scale"] = QJsonArray{1, 2};
        objects[0] = first;
        broken = valid;
        broken["objects"] = objects;
        require(!loaded.fromJson(broken, error) && loaded.toJson() == valid, "Reject malformed transform atomically");
        QFile corrupt(directory.filePath("Broken.elscene"));
        require(corrupt.open(QIODevice::WriteOnly), "Create corrupt file");
        corrupt.write("not JSON");
        corrupt.close();
        require(!loaded.load(corrupt.fileName(), error) && loaded.toJson() == valid, "Invalid load preserves scene");
        require(!loaded.save(directory.filePath("missing/Example.elscene"), error), "Report save failure");

        Scene other;
        other.createObject("Other");
        other.recordEdit();
        require(other.undo() && other.getObjects().empty(), "Independent scene undo");
        require(loaded.toJson() == valid, "Other history cannot change loaded scene");
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
