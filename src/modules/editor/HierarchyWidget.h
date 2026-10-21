#pragma once

#include <QWidget>
#include <QHash>
#include <QInputDialog>

#include "HierarchyTree.h"


class Scene;
class SceneObject;
class QTreeWidget;
class QTreeWidgetItem;


class HierarchyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit HierarchyWidget(
        Scene& scene,
        QWidget* parent = nullptr
    );


    void refresh();
    void selectObject(SceneObject* object);

    
    void setScene(Scene& scene);

signals:
    void objectSelected(SceneObject* object);

    void createCubeRequested();
    void createObjectRequested();
    void createEmptyRequested();

    void sceneModified();


    
    void statusText(QString&  message);


private:
    Scene* scene_;
    QMetaObject::Connection sceneChangedConnection_;
    QMetaObject::Connection sceneResetConnection_;
    QTreeWidget* tree_ = nullptr;

    QHash<QTreeWidgetItem*, SceneObject*> itemObjects_;
};