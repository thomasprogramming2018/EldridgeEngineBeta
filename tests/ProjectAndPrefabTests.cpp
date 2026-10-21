#include "modules/project/ProjectFile.h"
#include "modules/material/MaterialComponent.h"
#include "modules/componentsBackend/TransformComponent/TransformComponent.h"
#include "modules/componentsBackend/MeshComponent/MeshComponent.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv)
{
    QCoreApplication app(argc,argv);
    try
    {
        QTemporaryDir temp;
        QDir directory(temp.path()); directory.mkdir("Original"); directory.mkdir("Project");
        QString source = directory.filePath("Original/Custom.elsha");
        require(QFile::copy(QString(ENGINE_SHADER_DIRECTORY)+"/PBR.elsha",source),"Create shader source");
        QString texture = directory.filePath("Original/Albedo.png");
        QImage image(2,2,QImage::Format_RGB32); image.fill(Qt::red); require(image.save(texture),"Create texture");
        SceneManager manager;
        auto& first = manager.createScene("First");
        auto& root = first.createCube("Root");
        auto& child = first.createCube("Child"); child.setParent(&root);
        child.getComponent<TransformComponent>()->position = {1,2,3};
        auto& material = root.addComponent<MaterialComponent>();
        material.material = std::make_shared<Material>();
        material.material->shaderPath = source;
        material.material->parameters["material.diffuse"] = TexturePath{texture};
        auto& second = manager.createScene("Second"); second.createObject("Other"); manager.SetActiveScene(second);
        QString path = directory.filePath("Project/Test.elproject"), error;
        QJsonObject graphics {{"ssr",true},{"exposure",2.0},{"cameraNear",0.5},{"cameraFar",5000.0}};
        require(ProjectFile::save(path,manager,graphics,error),"Save portable project");
        QString prefab = directory.filePath("Project/Example.elprefab");
        require(ProjectFile::savePrefab(prefab,first,root,error),"Save hierarchy prefab");
        require(QDir(directory.filePath("Original")).removeRecursively(),"Remove original assets");
        require(directory.rename("Project","Moved"),"Move project folder");
        path = directory.filePath("Moved/Test.elproject"); prefab = directory.filePath("Moved/Example.elprefab");
        std::vector<std::unique_ptr<Scene>> loaded;
        int active=-1; QJsonObject restored;
        require(ProjectFile::load(path,loaded,active,restored,error),"Load moved project");
        require(loaded.size()==2 && active==1 && restored==graphics,"Restore tabs, active scene and settings");
        require(loaded[0]->getObjects()[1]->parent()==loaded[0]->getObjects()[0].get(),"Restore hierarchy");
        auto savedMaterial=loaded[0]->getObjects()[0]->getComponent<MaterialComponent>()->material;
        require(QFileInfo::exists(savedMaterial->shaderPath),"Rebase shader path");
        require(QFileInfo::exists(std::get<TexturePath>(savedMaterial->parameters["material.diffuse"]).path),"Rebase texture path");
        require(loaded[0]->getObjects()[0]->getComponent<MeshComponent>()->mesh->indices.size()==36,"Embedded geometry survives move");
        Scene target;
        target.createObject("Existing"); target.resetHistory();
        require(ProjectFile::loadPrefab(prefab,target,error),"Instantiate prefab");
        require(target.getObjects().size()==3 && target.getObjects()[2]->parent()==target.getObjects()[1].get(),"Prefab instance has independent parenting");
        require(ProjectFile::loadPrefab(prefab,target,error) && target.getObjects().size()==5,"Instantiate twice");
        require(target.getObjects()[4]->parent()==target.getObjects()[3].get(),"Second instance parent");
        require(target.undo() && target.getObjects().size()==3,"Undo prefab creation");
        require(QFile::remove(savedMaterial->shaderPath),"Remove a packaged shader");
        require(!ProjectFile::load(path,loaded,active,restored,error) && loaded.size()==2,"Missing assets fail without replacing current project");
    }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
