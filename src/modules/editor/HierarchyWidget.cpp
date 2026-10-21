#include "HierarchyWidget.h"
#include "HierarchyTree.h"
#include <QSignalBlocker>
#include "../scene/Scene.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QMenu>

HierarchyWidget::HierarchyWidget(
    Scene& scene,
    QWidget* parent
)
    : QWidget(parent), scene_(&scene)
{
    auto* layout = new QVBoxLayout(this);

    auto* tree = new HierarchyTree(this);
    tree_ = tree;
    
    connect(
        tree,
        &HierarchyTree::reparentRequested,
        this,
        [this](QTreeWidgetItem* dragged, QTreeWidgetItem* target)
        {
            auto* object = itemObjects_.value(dragged, nullptr);
            auto* parent = itemObjects_.value(target, nullptr);
        
            if (!object || !object->setParent(parent))
                return;
        
            scene_->recordEdit();
            refresh();
            emit sceneModified();
        }
    );
    tree_->setHeaderLabel("Scene Objects");

    tree_->setContextMenuPolicy(Qt::CustomContextMenu);

    layout->addWidget(tree_);

    setScene(scene);


    connect(
        tree_,
        &QTreeWidget::currentItemChanged,
        this,
        [this](QTreeWidgetItem* item, QTreeWidgetItem*)
        {
            SceneObject* object =
                itemObjects_.value(item, nullptr);

            emit objectSelected(object);
        }
    );


    connect(
        tree_,
        &QTreeWidget::customContextMenuRequested,
        this,
        [this](const QPoint& pos)
        {
            QMenu menu(this);

            QTreeWidgetItem* item = tree_->itemAt(pos);
            
            if (item)
            {
                QAction* renameA = menu.addAction("Rename");
                QAction* deleteA = menu.addAction("delete");


                QAction* selected = menu.exec(
                    tree_->viewport()->mapToGlobal(pos)
                );
                if (selected == renameA)
                {
                    SceneObject* object = itemObjects_.value(item, nullptr);
                    if (!object) return;
                
                    bool accepted = false;
                
                    QString newName = QInputDialog::getText(
                        this,
                        "Rename Object",
                        "Name:",
                        QLineEdit::Normal,
                        QString::fromStdString(object->name()),
                        &accepted
                    );
                
                    if (accepted && !newName.trimmed().isEmpty())
                    {
                        object->setName(newName.trimmed().toStdString());
                        scene_->recordEdit();
                    
                        item->setText(0, newName.trimmed());
                        emit sceneModified();
                        QString message = QString("object renamed to %1").arg(newName);
                        emit statusText(message);
                    }
                }
                else if (selected == deleteA)
                {

                    
                    SceneObject* object = itemObjects_.value(item, nullptr);
                    if (!object) return;
                
                    emit objectSelected(nullptr); // Clear Inspector first
                
                    if (scene_->removeObject(object))
                    {
                        scene_->recordEdit();
                        emit sceneModified();
                    }

                        QString message = QString("Deleted object");
                        emit statusText(message);
                    
                }


            }
            else
            {
                QAction* createObject = menu.addAction("Create Object (PBR / HDRP)...");
                QAction* createEmpty = menu.addAction("Create Empty");
                QAction* createCube = menu.addAction("Create Cube");


                QAction* selected = menu.exec(
                    tree_->viewport()->mapToGlobal(pos)
                );


                if (selected == createObject)
                {
                    emit createObjectRequested();
                }
                else if (selected == createEmpty)
                {
                    emit createEmptyRequested();
                    QString message = QString("Created Empty");
                    emit statusText(message);
                }
                else if (selected == createCube)
                {
                    emit createCubeRequested();

                    QString message = QString("Created Cube");
                    emit statusText(message);


                }

            }




        }
    );


}


void HierarchyWidget::refresh()
{
    auto* selected = itemObjects_.value(tree_->currentItem(), nullptr);
    SceneObject* restoredSelection = nullptr;

    {
        QSignalBlocker blocker(tree_);
        itemObjects_.clear();
        tree_->clear();

        QHash<SceneObject*, QTreeWidgetItem*> objectItems;

        // Create every item before attaching children to their parents.
        for (const auto& object : scene_->getObjects())
        {
            auto* item = new QTreeWidgetItem(
                QStringList{QString::fromStdString(object->name())}
            );
            objectItems.insert(object.get(), item);
            itemObjects_.insert(item, object.get());
        }

        for (const auto& object : scene_->getObjects())
        {
            auto* item = objectItems.value(object.get());
            auto* parentItem = objectItems.value(object->parent(), nullptr);

            if (parentItem)
                parentItem->addChild(item);
            else
                tree_->addTopLevelItem(item);
        }

        tree_->expandAll();
        if (auto* item = objectItems.value(selected, nullptr))
        {
            tree_->setCurrentItem(item);
            restoredSelection = selected;
        }
    }

    emit objectSelected(restoredSelection);
}







void HierarchyWidget::setScene(Scene& scene)
{
    disconnect(sceneChangedConnection_);
    disconnect(sceneResetConnection_);
    scene_ = &scene;
    sceneResetConnection_ = connect(scene_, &Scene::objectsAboutToReset, this, [this]()
    {
        emit objectSelected(nullptr);
        QSignalBlocker blocker(tree_);
        tree_->clear();
        itemObjects_.clear();
    });
    sceneChangedConnection_ = connect(scene_, &Scene::sceneChanged,
                                      this, &HierarchyWidget::refresh);
    {
        QSignalBlocker blocker(tree_);
        tree_->setCurrentItem(nullptr);
    }
    refresh();
}

void HierarchyWidget::selectObject(SceneObject* object)
{
    if(!object){tree_->clearSelection();tree_->setCurrentItem(nullptr);return;}
    for(auto it=itemObjects_.begin();it!=itemObjects_.end();++it)
        if(it.value()==object){tree_->setCurrentItem(it.key());tree_->scrollToItem(it.key());return;}
}
