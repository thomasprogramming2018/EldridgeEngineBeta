#pragma once


#include <vector>

#include <memory>
#include <filesystem>
#include <string>


#include <QObject>
#include <QJsonObject>

#include "SceneObject.h"

class Scene : public QObject
{
    Q_OBJECT
public:
    Scene() { resetHistory(); }
    ~Scene() = default;

    const QString& name() const { return name_; }
    void setName(const QString& name) { name_ = name; }


    
    SceneObject& createObject(std::string name = "Object");
    SceneObject& createCube(std::string name = "Cube");
    SceneObject& createLight(int type = 1, std::string name = "Light");
    SceneObject& createTerrain(std::string name = "Terrain");
    SceneObject& importModel(const std::filesystem::path& path);
    void CreateObject() { createObject(); }
    
    // Each history entry is a complete CPU-side scene snapshot.
    void resetHistory();
    void recordEdit();
    bool canUndo() const { return historyIndex_ > 0; }
    bool canRedo() const { return historyIndex_ + 1 < int(history_.size()); }
    bool undo();
    bool redo();
    bool save(const QString& path, QString& error);
    bool load(const QString& path, QString& error);
    const QString& filePath() const { return filePath_; }
    bool isModified() const;
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& data, QString& error);

    void initialize();
    void render();

    bool removeObject(SceneObject* object);

    std::vector<std::unique_ptr<SceneObject>>& getObjects() {return objects_;}
    const std::vector<std::unique_ptr<SceneObject>>& getObjects() const {return objects_;}
    void appendObjects(Scene& source);
    void markSaved();


    std::shared_ptr<SceneObject> selection = nullptr;

private:
    QString name_ = "Scene";
    QString filePath_;
    std::vector<QJsonObject> history_;
    int historyIndex_ = -1;
    QJsonObject savedState_;
    std::vector<std::unique_ptr<SceneObject>> objects_;

signals:
    void sceneChanged();
    void objectsAboutToReset();
    void historyChanged();
};
