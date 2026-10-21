#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include "modules/editor/LightingDemo.h"
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QTimer>
#include "modules/project/ProjectFile.h"
#include "modules/editor/ObjectCreationDialog.h"
#include "modules/editor/LoadingScreen.h"
#include "modules/editor/RayTraceDialog.h"
#include "modules/componentsBackend/TerrainComponent/TerrainComponent.h"
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <functional>
#include <QTabBar>
#include <QSignalBlocker>
#include <QAction>
#include <QDockWidget>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include "modules/componentsBackend/LightComponent/LightComponent.h"
#include <QVBoxLayout>
#include "modules/scene/SceneManager.h"
#include "modules/renderer/Renderer.h"
#include "modules/editor/MaterialEditor.h"
#include "modules/material/MaterialComponent.h"
#include "modules/editor/InspectorWidget.h"
#include "modules/editor/HierarchyWidget.h"
#include "modules/object_factory/ObjectFactory.h"

// Let the editor cancel closing when a scene has unsaved changes.
class EditorWindow : public QMainWindow
{
public:
    std::function<bool()> canClose;

protected:
    void closeEvent(QCloseEvent* event) override
    {
        if (!canClose || canClose()) event->accept();
        else event->ignore();
    }
};

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("EngineLabs");
    app.setStyle("Fusion");
    QSurfaceFormat format;
    format.setVersion(3,3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    //app.setStyleSheet(R"(
    //    QMainWindow, QWidget { background: #20232a; color: #d9dde5; font-size: 12px; }
    //    QMainWindow::separator { background: #353b46; width: 5px; height: 5px; }
    //    QMenuBar, QToolBar, QStatusBar { background: #272c35; border: none; }
    //    QMenuBar::item:selected { background: #3e4654; }
    //    QDockWidget { color: #e0e4eb; font-weight: bold; }
    //    QDockWidget::title { background: #2a303a; padding: 8px; border-bottom: 1px solid #424957; }
    //    QLabel#sectionTitle { color: #aeb9ce; font-size: 10px; font-weight: bold; }
    //    QLabel#mutedLabel { color: #8c98ae; }
    //    QPushButton, QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox { background: #303641; border: 1px solid #4c5666; border-radius: 4px; padding: 5px; }
    //    QPushButton:hover, QComboBox:hover { border-color: #6b91cc; background: #3b4657; }
    //    QPushButton:pressed { background: #476086; }
    //    QComboBox QAbstractItemView { background: #303641; selection-background-color: #4d6590; }
    //    QScrollArea { border: none; }
    //    QScrollBar:vertical { background: #252a33; width: 9px; }
    //    QScrollBar::handle:vertical { background: #505c6d; min-height: 24px; border-radius: 4px; }
    //    QTextEdit { background: #171c24; border: none; color: #a7b6cb; }
    //    QCheckBox { spacing: 6px; }
    //)");

    LoadingScreen splash;
    splash.show();
    splash.progress("Preparing editor...");
    app.processEvents();

    SceneManager scenes;
    Scene& scene = scenes.createScene("Main Scene");


    EditorWindow window;
    window.setWindowTitle("EngineLabs  |  Scene View");
    window.resize(1440,850);
    window.setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);

    auto* viewport=new Renderer(scene,&window);
    auto* scenePane = new QWidget(&window);
    auto* sceneLayout = new QVBoxLayout(scenePane);
    sceneLayout->setContentsMargins(0, 0, 0, 0);
    auto* sceneTabs = new QTabBar(scenePane);
    sceneTabs->setExpanding(false);
    sceneTabs->setObjectName("sceneTabs");
    int mainTab = sceneTabs->addTab(scene.name());
    sceneTabs->setTabData(mainTab, QVariant::fromValue(&scene));
    sceneLayout->addWidget(sceneTabs);
    sceneLayout->addWidget(viewport, 1);
    window.setCentralWidget(scenePane);
    QObject::connect(viewport, &Renderer::sceneResourcesReady, &window,
                     [&]() { if (splash.isVisible()) splash.finish(&window); }, Qt::QueuedConnection);


    SceneObject& cube = scene.createCube("Cube");



    // After constructing viewport:
    auto& materialComponent = cube.addComponent<MaterialComponent>();
    materialComponent.material = viewport->material();
    materialComponent.material->shaderPath = QString(ENGINE_SHADER_DIRECTORY) + "/PBR.elsha";
    scene.resetHistory();

    auto* toolbar=window.addToolBar("Scene Tools");
    toolbar->setMovable(false);
    toolbar->addWidget(new QLabel("  EldridgeEngine     |     SCENE VIEW     ",toolbar));
    toolbar->addSeparator();
    auto* focus=new QPushButton("Focus Cube  [F]",toolbar);
    toolbar->addWidget(focus);
    QObject::connect(focus,&QPushButton::clicked,viewport,&Renderer::focusCube);

    auto* inspectorDock=new QDockWidget("Inspector",&window);
    inspectorDock->setObjectName("inspectorDock");
    inspectorDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* scroll=new QScrollArea(inspectorDock);
    scroll->setWidgetResizable(true);
    auto* inspector = new InspectorWidget(scroll);

    scroll->setWidget(inspector);
    inspector->setObject(nullptr);



    QObject::connect(inspector, &InspectorWidget::materialChanged,
            viewport, [viewport]() { viewport->update(); });

    QObject::connect(
        viewport,
        &Renderer::shaderChanged,
        inspector,
        &InspectorWidget::refresh,
        Qt::QueuedConnection
    );

    QObject::connect(viewport, &Renderer::sceneResourcesReady,
                     inspector, &InspectorWidget::refresh, Qt::QueuedConnection);

    inspectorDock->setWidget(scroll);
    window.addDockWidget(Qt::RightDockWidgetArea,inspectorDock);
    inspectorDock->setMinimumWidth(310);
    inspectorDock->resize(390,700);


    



    // The left panel is a camera guide, not a scene Hierarchy. Hierarchy is our NEXT feature.
    auto* toolsDock=new QDockWidget("Scene Tools",&window);
    toolsDock->setObjectName("toolsDock");
    auto* toolsPane=new QWidget(toolsDock);
    auto* toolsLayout=new QVBoxLayout(toolsPane);

    auto* hierarchy = new HierarchyWidget(scene, toolsPane);

    toolsLayout->insertWidget(0, hierarchy);
    toolsDock->setWindowTitle("Hierarchy");

    auto* sceneLabel=new QLabel("SCENE CAMERA",toolsPane);
    sceneLabel->setObjectName("sectionTitle");
    toolsLayout->addWidget(sceneLabel);

    ObjectFactory factory(scene);

    QString projectPath;
    bool projectModified = false;
    SceneObject* selectedObject = nullptr;
    QObject::connect(hierarchy, &HierarchyWidget::objectSelected, &window,
                     [&](SceneObject* object) { selectedObject = object; });
    auto* fileMenu = window.menuBar()->addMenu("File");
    auto* editMenu = window.menuBar()->addMenu("Edit");
    auto* undo = editMenu->addAction("Undo");
    auto* redo = editMenu->addAction("Redo");
    undo->setShortcut(QKeySequence("Ctrl+Z"));
    redo->setShortcuts({QKeySequence("Ctrl+Y"), QKeySequence("Ctrl+Shift+Z")});

    auto updateSceneActions = [&]()
    {
        undo->setEnabled(scenes.activeScene->canUndo());
        redo->setEnabled(scenes.activeScene->canRedo());
        for (int i = 0; i < sceneTabs->count(); ++i)
        {
            auto* current = sceneTabs->tabData(i).value<Scene*>();
            if (current) sceneTabs->setTabText(i, current->name() + (current->isModified() ? " *" : ""));
        }
    };
    auto watchScene = [&](Scene& current)
    {
        projectModified = true;
        QObject::connect(&current, &Scene::historyChanged, &window, updateSceneActions);
        QObject::connect(&current, &Scene::historyChanged, &window, [&]() { projectModified = true; });
    };
    watchScene(scene);
    updateSceneActions();

    QObject::connect(undo, &QAction::triggered, &window, [&]() { scenes.activeScene->undo(); });
    QObject::connect(redo, &QAction::triggered, &window, [&]() { scenes.activeScene->redo(); });
    QObject::connect(inspector, &InspectorWidget::transformChanged, &window, [&]()
    {
        scenes.activeScene->recordEdit();
        viewport->update();
    });
    QObject::connect(inspector, &InspectorWidget::materialChanged, &window, [&]()
    {
        scenes.activeScene->recordEdit();
    });
    bool firstShaderLoad = true;
    QObject::connect(viewport, &Renderer::shaderChanged, &window, [&]()
    {
        if (firstShaderLoad)
        {
            scene.resetHistory();
            firstShaderLoad = false;
        }
        else scenes.activeScene->recordEdit();
    });

    auto saveScene = [&](Scene& current, bool choosePath)
    {
        QString path = current.filePath();
        if (choosePath || path.isEmpty())
            path = QFileDialog::getSaveFileName(&window, "Save Scene", path.isEmpty() ? current.name() + ".elscene" : path,
                                               "Eldridge Scene (*.elscene)");
        if (path.isEmpty()) return false;
        if (!path.endsWith(".elscene")) path += ".elscene";
        QString error;
        if (!current.save(path, error))
        {
            QMessageBox::warning(&window, "Save Scene", error);
            return false;
        }
        window.statusBar()->showMessage("Saved " + path, 5000);
        return true;
    };
    auto* save = fileMenu->addAction("Save Scene");
    save->setShortcut(QKeySequence("Ctrl+S"));
    QObject::connect(save, &QAction::triggered, &window, [&]() { saveScene(*scenes.activeScene, false); });
    auto* saveAs = fileMenu->addAction("Save Scene As...");
    saveAs->setShortcut(QKeySequence("Ctrl+Shift+S"));
    QObject::connect(saveAs, &QAction::triggered, &window, [&]() { saveScene(*scenes.activeScene, true); });

    auto* open = fileMenu->addAction("Open Scene...");
    open->setShortcut(QKeySequence("Ctrl+O"));
    QObject::connect(open, &QAction::triggered, &window, [&]()
    {
        QString path = QFileDialog::getOpenFileName(&window, "Open Scene", QString(), "Eldridge Scene (*.elscene)");
        if (path.isEmpty()) return;
        path = QFileInfo(path).absoluteFilePath();
        for (int i = 0; i < sceneTabs->count(); ++i)
        {
            auto* current = sceneTabs->tabData(i).value<Scene*>();
            if (current && current->filePath() == path)
            {
                if (current->isModified())
                {
                    auto choice = QMessageBox::question(&window, "Reload Scene",
                        "Reload the saved scene? Unsaved changes in this tab will be discarded.",
                        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
                    if (choice != QMessageBox::Yes) return;
                }
                QString error;
                if (!current->load(path, error))
                {
                    QMessageBox::warning(&window, "Open Scene", error);
                    return;
                }
                sceneTabs->setCurrentIndex(i);
                window.setWindowTitle("EngineLabs  |  " + current->name());
                viewport->update();
                return;
            }
        }
        auto loaded = std::make_unique<Scene>();
        QString error;
        if (!loaded->load(path, error))
        {
            QMessageBox::warning(&window, "Open Scene", error);
            return;
        }
        Scene& opened = scenes.addScene(std::move(loaded));
        watchScene(opened);
        QSignalBlocker blocker(sceneTabs);
        int index = sceneTabs->addTab(opened.name());
        sceneTabs->setTabData(index, QVariant::fromValue(&opened));
        blocker.unblock();
        sceneTabs->setCurrentIndex(index);
    });

    window.canClose = [&]()
    {
        for (const auto& current : scenes.scenes())
        {
            if (!current->isModified()) continue;
            auto choice = QMessageBox::question(&window, "Unsaved Scene",
                "Save changes to " + current->name() + "?",
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
            if (choice == QMessageBox::Cancel) return false;
            if (choice == QMessageBox::Save && !saveScene(*current, false)) return false;
        }
        return true;
    };


    QObject::connect(sceneTabs, &QTabBar::currentChanged, &window, [&](int index)
    {
        if (index < 0)
            return;
        auto* selectedScene = sceneTabs->tabData(index).value<Scene*>();
        if (!selectedScene)
            return;

        inspector->setObject(nullptr);
        scenes.SetActiveScene(*selectedScene);
        viewport->setScene(*selectedScene);
        hierarchy->setScene(*selectedScene);
        factory.setScene(*selectedScene);
        window.setWindowTitle("EngineLabs  |  " + selectedScene->name());
        updateSceneActions();
    });

    auto* newScene = toolbar->addAction("New Scene");
    fileMenu->insertAction(save, newScene);
    newScene->setShortcut(QKeySequence("Ctrl+N"));
    QObject::connect(newScene, &QAction::triggered, &window, [&]()
    {
        Scene& created = scenes.createScene(
            QString("Scene %1").arg(sceneTabs->count() + 1));
        watchScene(created);
        // Block switching until the tab has its scene pointer.
        QSignalBlocker blocker(sceneTabs);
        int index = sceneTabs->addTab(created.name());
        sceneTabs->setTabData(index, QVariant::fromValue(&created));
        blocker.unblock();
        sceneTabs->setCurrentIndex(index);
    });

    
    QObject::connect(
        hierarchy,
        &HierarchyWidget::sceneModified,
        &window,
        [&]()
        {
            hierarchy->refresh();
            viewport->update();
        });

    QObject::connect(
        hierarchy,
        &HierarchyWidget::createCubeRequested,
        &window,
        [&]()
        {
            factory.createCube(viewport->material());
            scenes.activeScene->recordEdit();
            hierarchy->refresh();
            viewport->update();
        });

    QObject::connect(
        hierarchy,
        &HierarchyWidget::createEmptyRequested,
        &window,
        [&]()
        {
            factory.createEmpty();
            scenes.activeScene->recordEdit();
            hierarchy->refresh();
            viewport->update();
        });

    QObject::connect(
        hierarchy,
        &HierarchyWidget::objectSelected,
        inspector,
        &InspectorWidget::setObject
    );

    QObject::connect(
        inspector,
        &InspectorWidget::shaderChangeRequested,
        viewport,
        &Renderer::changeMaterialShader
    );

    


    toolsLayout->addSpacing(30);

    toolsLayout->addStretch();
    toolsDock->setWidget(toolsPane);
    window.addDockWidget(Qt::LeftDockWidgetArea,toolsDock);
    toolsDock->setMinimumWidth(170);

    QObject::connect(inspector, &InspectorWidget::customShaderRequested,
                     viewport, &Renderer::loadMaterialShader);

    auto showError = [&](const QString& error) { QMessageBox::warning(&window, "Eldridge", error); };
    QObject::connect(hierarchy,&HierarchyWidget::objectSelected,viewport,&Renderer::setSelectedObject);
    QObject::connect(viewport,&Renderer::objectPicked,hierarchy,&HierarchyWidget::selectObject);
    QObject::connect(viewport,&Renderer::transformEdited,inspector,&InspectorWidget::refresh);
    QObject::connect(inspector,&InspectorWidget::lightChanged,&window,[&]()
    {scenes.activeScene->recordEdit();viewport->invalidateLighting();});
    auto* gizmoMode=new QComboBox(&window);gizmoMode->addItems({"Move [W]","Rotate [E]","Scale [R]"});
    toolbar->addWidget(gizmoMode);
    QObject::connect(gizmoMode,&QComboBox::currentIndexChanged,viewport,&Renderer::setGizmoMode);
    QObject::connect(viewport,&Renderer::gizmoModeChanged,gizmoMode,&QComboBox::setCurrentIndex);
    auto* localAxes=new QCheckBox("Local",&window);toolbar->addWidget(localAxes);
    QObject::connect(localAxes,&QCheckBox::toggled,viewport,&Renderer::setGizmoLocal);
    auto* snapTransforms=new QCheckBox("Snap",&window);toolbar->addWidget(snapTransforms);
    snapTransforms->setToolTip("Move: 0.5 units, rotate: 15 degrees, scale: 0.1. Escape cancels a drag. Hold right mouse to fly the camera.");
    QObject::connect(snapTransforms,&QCheckBox::toggled,viewport,&Renderer::setGizmoSnap);
    auto* lightButton=new QToolButton(&window);lightButton->setText("Create Light");
    lightButton->setPopupMode(QToolButton::InstantPopup);auto* lightMenu=new QMenu(lightButton);lightButton->setMenu(lightMenu);
    const QStringList lightNames={"Directional Light","Point Light","Spot Light","Rectangle Light"};
    for(int type=0;type<4;++type)
    {
        auto* action=lightMenu->addAction(lightNames[type]);
        QObject::connect(action,&QAction::triggered,&window,[&,type]()
        {
            auto& object=scenes.activeScene->createLight(type,lightNames[type].toStdString());
            auto* transform=object.getComponent<TransformComponent>();
            transform->position=viewport->camera.position+viewport->camera.forward()*4;
            if(type==0||type==2||type==3)transform->rotation={-45,0,0};
            scenes.activeScene->recordEdit();hierarchy->refresh();hierarchy->selectObject(&object);
        });
    }
    toolbar->addWidget(lightButton);
    auto* createTerrain = toolbar->addAction("Create Terrain");
    QObject::connect(createTerrain,&QAction::triggered,&window,[&]()
    {
        auto& object=scenes.activeScene->createTerrain();
        scenes.activeScene->recordEdit();
        hierarchy->refresh();hierarchy->selectObject(&object);viewport->focusObject(&object);
    });
    QObject::connect(inspector,&InspectorWidget::frameTerrainRequested,&window,[&](TerrainComponent* terrain)
    {for(const auto& object:scenes.activeScene->getObjects())if(object->getComponent<TerrainComponent>()==terrain)viewport->focusObject(object.get());});
    QObject::connect(inspector,&InspectorWidget::terrainChanged,&window,[&]()
    {scenes.activeScene->recordEdit();viewport->invalidateScene();});
    QObject::connect(inspector,&InspectorWidget::terrainBrushRequested,viewport,&Renderer::setTerrainBrush);
    QObject::connect(hierarchy,&HierarchyWidget::objectSelected,&window,[&](SceneObject*)
    {viewport->setTerrainBrush(nullptr,0,4,0.3f,0,0);});
    auto* create = toolbar->addAction("Create Object...");
    QObject::connect(hierarchy, &HierarchyWidget::createObjectRequested, create, &QAction::trigger);
    QObject::connect(create, &QAction::triggered, &window, [&]()
    {
        ObjectCreationDialog dialog(&window);
        if (dialog.exec() != QDialog::Accepted) return;
        try
        {
            SceneObject* object;
            if (dialog.objectType() == "OBJ Mesh")
            {
                LoadingScreen loading;
                loading.show(); loading.progress("Importing object geometry...");
                app.processEvents(QEventLoop::ExcludeUserInputEvents);
                object = &scenes.activeScene->importModel(dialog.meshPath().toStdString());
            }
            else if(dialog.objectType().endsWith("Light"))
                object=&scenes.activeScene->createLight(lightNames.indexOf(dialog.objectType()),dialog.objectName().toStdString());
            else if (dialog.objectType() == "Terrain") object = &scenes.activeScene->createTerrain(dialog.objectName().toStdString());
            else if (dialog.objectType() == "Empty") object = &scenes.activeScene->createObject(dialog.objectName().toStdString());
            else object = &scenes.activeScene->createCube(dialog.objectName().toStdString());
            object->setName(dialog.objectName().toStdString());
            if (dialog.objectType() != "Empty" && dialog.objectType() != "Terrain" && !dialog.objectType().endsWith("Light"))
            {
                auto& material = object->addComponent<MaterialComponent>();
                material.material = std::make_shared<Material>();
                material.material->shaderPath = QString(ENGINE_SHADER_DIRECTORY) + "/" + dialog.shaderName() + ".elsha";
            }
            scenes.activeScene->recordEdit();
            hierarchy->refresh();
            hierarchy->selectObject(object);
            if(dialog.objectType()=="Terrain") viewport->focusObject(object);
            viewport->update();
        }
        catch (const std::exception& error) { showError(error.what()); }
    });

    auto* exportPrefab = fileMenu->addAction("Save Selected Prefab...");
    QObject::connect(exportPrefab, &QAction::triggered, &window, [&]()
    {
        if (!selectedObject) { showError("Select an object in the hierarchy first."); return; }
        QString path = QFileDialog::getSaveFileName(&window, "Save Prefab", "Object.elprefab", "Eldridge Prefab (*.elprefab)");
        if (path.isEmpty()) return;
        if (!path.endsWith(".elprefab")) path += ".elprefab";
        QString error;
        if (!ProjectFile::savePrefab(path, *scenes.activeScene, *selectedObject, error)) showError(error);
    });
    auto* importPrefab = fileMenu->addAction("Load Prefab...");
    QObject::connect(importPrefab, &QAction::triggered, &window, [&]()
    {
        QString path = QFileDialog::getOpenFileName(&window, "Load Prefab", QString(), "Eldridge Prefab (*.elprefab)");
        if (path.isEmpty()) return;
        QString error;
        if (!ProjectFile::loadPrefab(path, *scenes.activeScene, error)) showError(error);
    });

    auto saveProject = [&](bool choosePath)
    {
        QString path = projectPath;
        if (choosePath || path.isEmpty())
            path = QFileDialog::getSaveFileName(&window, "Save Project", "Project.elproject", "Eldridge Project (*.elproject)");
        if (path.isEmpty()) return false;
        if (!path.endsWith(".elproject")) path += ".elproject";
        auto graphics = viewport->renderSettings.toJson();
        graphics["cameraNear"]=viewport->camera.nearPlane;
        graphics["cameraFar"]=viewport->camera.farPlane;
        graphics["camera"] = QJsonArray{viewport->camera.position.x(), viewport->camera.position.y(), viewport->camera.position.z(),
                                        viewport->camera.yaw_, viewport->camera.pitch_};
        QString error;
        if (!ProjectFile::save(path, scenes, graphics, error)) { showError(error); return false; }
        projectPath = path;
        for (const auto& current : scenes.scenes()) current->markSaved();
        projectModified = false;
        window.statusBar()->showMessage("Project saved: " + path, 5000);
        return true;
    };
    auto* saveProjectAction = fileMenu->addAction("Save Project...");
    saveProjectAction->setShortcut(QKeySequence("Ctrl+Alt+S"));
    QObject::connect(saveProjectAction, &QAction::triggered, &window, [&]() { saveProject(false); });
    auto* saveProjectAs = fileMenu->addAction("Save Project As...");
    QObject::connect(saveProjectAs, &QAction::triggered, &window, [&]() { saveProject(true); });
    auto* loadProject = fileMenu->addAction("Open Project...");
    loadProject->setShortcut(QKeySequence("Ctrl+Alt+O"));
    QObject::connect(loadProject, &QAction::triggered, &window, [&]()
    {
        QString path = QFileDialog::getOpenFileName(&window, "Open Project", QString(), "Eldridge Project (*.elproject)");
        if (path.isEmpty()) return;
        std::vector<std::unique_ptr<Scene>> loaded;
        QJsonObject graphics;
        int active = 0;
        QString error;
        LoadingScreen loading;
        loading.show(); loading.progress("Loading project and assets...");
        app.processEvents(QEventLoop::ExcludeUserInputEvents);
        bool success = ProjectFile::load(path, loaded, active, graphics, error);
        loading.hide();
        if (!success) { showError(error); return; }
        if (window.canClose && !window.canClose()) return;
        // Saving in the confirmation can change the file being opened.
        if (!ProjectFile::load(path, loaded, active, graphics, error)) { showError(error); return; }
        inspector->setObject(nullptr);
        selectedObject = nullptr;
        viewport->setScene(*loaded[active]);
        hierarchy->setScene(*loaded[active]);
        factory.setScene(*loaded[active]);
        QSignalBlocker blocker(sceneTabs);
        while (sceneTabs->count() > 0) sceneTabs->removeTab(0);
        scenes.replaceScenes(std::move(loaded), active);
        for (const auto& current : scenes.scenes())
        {
            watchScene(*current);
            int index = sceneTabs->addTab(current->name());
            sceneTabs->setTabData(index, QVariant::fromValue(current.get()));
        }
        sceneTabs->setCurrentIndex(active);
        viewport->renderSettings.fromJson(graphics);
        auto camera = graphics["camera"].toArray();
        if (camera.size() == 5)
        {
            viewport->camera.position = QVector3D(camera[0].toDouble(), camera[1].toDouble(), camera[2].toDouble());
            viewport->camera.yaw_ = camera[3].toDouble();
            viewport->camera.pitch_ = std::clamp(float(camera[4].toDouble()), -89.f, 89.f);
        }
        viewport->camera.nearPlane=graphics.value("cameraNear").toDouble(0.05);
        viewport->camera.farPlane=graphics.value("cameraFar").toDouble(500);
        projectPath = path;
        projectModified = false;
        updateSceneActions();
        window.setWindowTitle("EngineLabs  |  " + scenes.activeScene->name());
    });
    auto closeScenes = window.canClose;
    window.canClose = [&, closeScenes]()
    {
        if (!projectPath.isEmpty() && projectModified)
        {
            auto answer = QMessageBox::question(&window, "Unsaved Project", "Save the project before closing?",
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
            if (answer == QMessageBox::Cancel) return false;
            if (answer == QMessageBox::Save) return saveProject(false);
            return true;
        }
        return closeScenes();
    };

    auto* graphicsDock = new QDockWidget("Graphics", &window);
    auto* graphicsPane = new QWidget(graphicsDock);
    auto* graphicsForm = new QFormLayout(graphicsPane);
    auto addEffect = [&](const QString& label, bool RenderSettings::* field)
    {
        auto* check = new QCheckBox(graphicsPane);
        check->setChecked(viewport->renderSettings.*field);
        graphicsForm->addRow(label, check);
        QObject::connect(check, &QCheckBox::toggled, &window, [&, field](bool enabled)
        { viewport->renderSettings.*field = enabled; projectModified = true; viewport->update(); });
        QObject::connect(loadProject, &QAction::triggered, check, [&, check, field]()
        { QSignalBlocker block(check); check->setChecked(viewport->renderSettings.*field); });
        return check;
    };
    addEffect("Shadow maps", &RenderSettings::shadows);
    auto* bloomCheck = addEffect("Bloom", &RenderSettings::bloom);
    addEffect("Ambient occlusion", &RenderSettings::ambientOcclusion);
    addEffect("Screen-space reflections", &RenderSettings::screenSpaceReflections);
    auto* raysCheck = addEffect("God rays", &RenderSettings::godRays);
    auto* fogCheck = addEffect("Volumetric height fog", &RenderSettings::volumetricFog);
    auto* exposure = new QDoubleSpinBox(graphicsPane);
    exposure->setRange(0.1, 8); exposure->setValue(1); exposure->setSingleStep(0.1);
    graphicsForm->addRow("Exposure", exposure);
    QObject::connect(exposure, &QDoubleSpinBox::valueChanged, &window, [&](double value)
    { viewport->renderSettings.exposure = value; projectModified = true; viewport->update(); });
    QObject::connect(loadProject, &QAction::triggered, exposure, [&]()
    { QSignalBlocker block(exposure); exposure->setValue(viewport->renderSettings.exposure); });
    auto* fogDensity = new QDoubleSpinBox(graphicsPane);
    fogDensity->setRange(0, 0.3); fogDensity->setDecimals(3); fogDensity->setSingleStep(0.005);
    fogDensity->setValue(viewport->renderSettings.fogDensity);
    graphicsForm->addRow("Fog density", fogDensity);
    QObject::connect(fogDensity, &QDoubleSpinBox::valueChanged, &window, [&](double value)
    { viewport->renderSettings.fogDensity = value; projectModified = true; viewport->update(); });
    QObject::connect(loadProject, &QAction::triggered, fogDensity, [&]()
    { QSignalBlocker block(fogDensity); fogDensity->setValue(viewport->renderSettings.fogDensity); });
    auto support=VulkanRayTracer::support();
    auto* hardware=new QCheckBox(graphicsPane);
    hardware->setEnabled(support.available);
    hardware->setToolTip(support.available?"Vulkan ray queries on "+support.device:support.reason);
    graphicsForm->addRow("Realtime GPU renderer",hardware);
    auto* gpuName=new QLabel(support.available?support.device:support.reason,graphicsPane);
    gpuName->setWordWrap(true);graphicsForm->addRow(gpuName);
    QObject::connect(hardware,&QCheckBox::toggled,&window,[&](bool enabled)
    {viewport->renderSettings.hardwareRayTracing=enabled;projectModified=true;viewport->update();});
    QObject::connect(viewport,&Renderer::hardwareRayTracingFailed,hardware,[&](const QString& error)
    {hardware->setChecked(false);hardware->setToolTip(error);});
    auto* renderMode = new QComboBox(graphicsPane);
    renderMode->addItems({"Reference lighting (path tracing)", "Hybrid lighting"});
    renderMode->setCurrentIndex(viewport->renderSettings.rayFullFrame ? 0 : 1);
    graphicsForm->addRow("Viewport shading", renderMode);
    QObject::connect(renderMode, &QComboBox::currentIndexChanged, &window, [&](int mode)
    { viewport->renderSettings.rayFullFrame = mode == 0; projectModified = true; viewport->update(); });
    auto* preset = new QComboBox(graphicsPane);
    preset->addItems({"Fast", "Balanced", "Detailed", "Custom"});
    preset->setCurrentIndex(1);
    preset->setToolTip("Changes trace resolution and convergence samples. Camera movement uses a smaller buffer.");
    graphicsForm->addRow("GPU quality", preset);
    auto* raySamples = new QSpinBox(graphicsPane);
    raySamples->setRange(1, 512); raySamples->setValue(viewport->renderSettings.raySamples);
    auto* rayWidth = new QSpinBox(graphicsPane);
    rayWidth->setRange(160, 1920); rayWidth->setSingleStep(32); rayWidth->setValue(viewport->renderSettings.rayWidth);
    auto* rayBudget = new QDoubleSpinBox(graphicsPane);
    rayBudget->setRange(4, 40); rayBudget->setValue(12); rayBudget->setSuffix(" ms");
    rayBudget->setToolTip("Adapt the lighting resolution to trace + transfer time. This is not a whole-frame FPS guarantee.");
    auto* rayStrength = new QDoubleSpinBox(graphicsPane);
    rayStrength->setRange(0, 4); rayStrength->setSingleStep(0.1); rayStrength->setValue(1);
    auto* denoise = new QCheckBox(graphicsPane); denoise->setChecked(true);
    addEffect("Ray traced global illumination", &RenderSettings::rayGI);
    auto rayFloat=[&](const QString& label,float RenderSettings::* field,double minimum,double maximum,double step)
    {
        auto* input=new QDoubleSpinBox(graphicsPane);input->setRange(minimum,maximum);input->setSingleStep(step);input->setDecimals(2);
        input->setValue(viewport->renderSettings.*field);graphicsForm->addRow(label,input);
        QObject::connect(input,&QDoubleSpinBox::valueChanged,&window,[&,field](double value)
        {viewport->renderSettings.*field=float(value);projectModified=true;viewport->update();});
        QObject::connect(loadProject,&QAction::triggered,input,[&,input,field]()
        {QSignalBlocker block(input);input->setValue(viewport->renderSettings.*field);});
        return input;
    };
    auto rayInteger=[&](const QString& label,int RenderSettings::* field,int minimum,int maximum)
    {
        auto* input=new QSpinBox(graphicsPane);input->setRange(minimum,maximum);input->setValue(viewport->renderSettings.*field);graphicsForm->addRow(label,input);
        QObject::connect(input,&QSpinBox::valueChanged,&window,[&,field](int value)
        {viewport->renderSettings.*field=value;projectModified=true;viewport->update();});
        QObject::connect(loadProject,&QAction::triggered,input,[&,input,field]()
        {QSignalBlocker block(input);input->setValue(viewport->renderSettings.*field);});
    };
    rayInteger("Vulkan filter passes",&RenderSettings::rayDenoisePasses,1,4);
    rayInteger("Samples while moving",&RenderSettings::rayMovingSamples,1,4);
    rayFloat("Vulkan filter strength",&RenderSettings::rayDenoiseStrength,.1,2,.1);
    auto* indirectClamp=rayFloat("Indirect firefly clamp",&RenderSettings::rayIndirectClamp,0,100,1);
    indirectClamp->setToolTip("Limits very bright indirect samples. Zero disables clamping for reference renders.");
    rayFloat("Sky intensity",&RenderSettings::raySkyIntensity,0,20,.1);
    auto* sunIntensity=rayFloat("Preview sun intensity",&RenderSettings::raySunIntensity,0,100,.1);
    sunIntensity->setToolTip("Used only when the scene contains no light components. Add a directional light to replace this sun.");
    rayFloat("Preview sun angle",&RenderSettings::raySunAngle,0,20,.1);
    auto* debugLighting = new QComboBox(graphicsPane);
    debugLighting->addItems({"Final image", "Ray radiance only", "Surface normals", "Temporal history"});
    auto* pathBounces = new QSpinBox(graphicsPane);
    pathBounces->setRange(1, 8); pathBounces->setValue(viewport->renderSettings.rayBounces);
    graphicsForm->addRow("Path bounces", pathBounces);
    QObject::connect(pathBounces, &QSpinBox::valueChanged, &window, [&](int value)
    { viewport->renderSettings.rayBounces = value; projectModified = true; viewport->update(); });
    graphicsForm->addRow("Convergence samples", raySamples);
    graphicsForm->addRow("Maximum trace width", rayWidth);
    graphicsForm->addRow("Lighting budget", rayBudget);
    graphicsForm->addRow("Hybrid indirect strength", rayStrength);
    graphicsForm->addRow("GPU edge-aware filter", denoise);
    graphicsForm->addRow("Lighting debug view", debugLighting);
    QObject::connect(raySamples, &QSpinBox::valueChanged, &window, [&](int value)
    { viewport->renderSettings.raySamples = value; projectModified = true; viewport->update(); });
    QObject::connect(rayWidth, &QSpinBox::valueChanged, &window, [&](int value)
    { viewport->renderSettings.rayWidth = value; projectModified = true; viewport->update(); });
    QObject::connect(rayBudget, &QDoubleSpinBox::valueChanged, &window, [&](double value)
    { viewport->renderSettings.rayBudgetMs = value; projectModified = true; viewport->update(); });
    QObject::connect(rayStrength, &QDoubleSpinBox::valueChanged, &window, [&](double value)
    { viewport->renderSettings.rayStrength = value; projectModified = true; viewport->update(); });
    QObject::connect(denoise, &QCheckBox::toggled, &window, [&](bool value)
    { viewport->renderSettings.rayDenoise = value; projectModified = true; viewport->update(); });
    QObject::connect(debugLighting, &QComboBox::currentIndexChanged, &window, [&](int value)
    { viewport->renderSettings.lightingDebug = value; projectModified = true; viewport->update(); });
    QObject::connect(preset, &QComboBox::currentIndexChanged, &window, [&](int value)
    {
        if (value > 2) return;
        const int widths[] = {480, 640, 960}, samples[] = {32, 128, 256};
        const int hybridWidths[] = {240, 320, 640}, hybridSamples[] = {16, 32, 64};
        rayWidth->setValue(viewport->renderSettings.rayFullFrame ? widths[value] : hybridWidths[value]);
        raySamples->setValue(viewport->renderSettings.rayFullFrame ? samples[value] : hybridSamples[value]);
    });
    QObject::connect(loadProject, &QAction::triggered, hardware, [&]()
    {
        if (!support.available) viewport->renderSettings.hardwareRayTracing = false;
        QSignalBlocker a(hardware), b(raySamples), c(rayWidth), d(denoise), e(rayBudget), f(rayStrength), g(debugLighting), h(renderMode), i(pathBounces), j(preset);
        hardware->setChecked(viewport->renderSettings.hardwareRayTracing);
        renderMode->setCurrentIndex(viewport->renderSettings.rayFullFrame ? 0 : 1);
        pathBounces->setValue(viewport->renderSettings.rayBounces);
        preset->setCurrentIndex(3);
        raySamples->setValue(viewport->renderSettings.raySamples);
        rayWidth->setValue(viewport->renderSettings.rayWidth);
        denoise->setChecked(viewport->renderSettings.rayDenoise);
        rayBudget->setValue(viewport->renderSettings.rayBudgetMs);
        rayStrength->setValue(viewport->renderSettings.rayStrength);
        debugLighting->setCurrentIndex(viewport->renderSettings.lightingDebug);
    });
    auto* lightingDemo = new QPushButton("Open Lighting Playground", graphicsPane);
    graphicsForm->addRow(lightingDemo);
    QObject::connect(lightingDemo, &QPushButton::clicked, &window, [&]()
    {
        Scene& demo = scenes.createScene("Lighting Playground");
        LightingDemo::populate(demo);
        watchScene(demo);
        QSignalBlocker blocker(sceneTabs);
        int index = sceneTabs->addTab(demo.name());
        sceneTabs->setTabData(index, QVariant::fromValue(&demo));
        blocker.unblock();
        sceneTabs->setCurrentIndex(index);
        LightingDemo::frame(viewport->camera);
        hardware->setChecked(support.available);
        debugLighting->setCurrentIndex(0);
        viewport->update();
    });
    auto* raytrace = new QPushButton("Path-Traced Reference / Export...", graphicsPane);
    graphicsForm->addRow(raytrace);
    QObject::connect(raytrace, &QPushButton::clicked, &window, [&]()
    {
        RayTraceDialog dialog(*scenes.activeScene, viewport->camera, &window);
        QObject::connect(&dialog, &RayTraceDialog::viewportRequested, &window, [&](int width, int samples)
        {
            renderMode->setCurrentIndex(0);
            hardware->setChecked(true);
            rayWidth->setValue(width); raySamples->setValue(samples); pathBounces->setValue(4);
            preset->setCurrentIndex(3);
            exposure->setValue(1); denoise->setChecked(true);
            bloomCheck->setChecked(false); raysCheck->setChecked(false); fogCheck->setChecked(false);
            debugLighting->setCurrentIndex(0);
            viewport->setFocus(); viewport->update();
        });
        dialog.exec();
    });
    auto* graphicsScroll=new QScrollArea(graphicsDock);
    graphicsScroll->setWidgetResizable(true);graphicsScroll->setWidget(graphicsPane);
    graphicsDock->setWidget(graphicsScroll);
    window.addDockWidget(Qt::RightDockWidgetArea, graphicsDock);

    auto* consoleDock=new QDockWidget("Console",&window);
    consoleDock->setObjectName("consoleDock");
    auto* console=new QTextEdit(consoleDock);
    console->setReadOnly(true);
    console->setPlainText("EngineLabs  ·  3D Scene View\nShader compilation/reflection messages appear here and in the terminal.\nReady to initialize OpenGL…");
    consoleDock->setWidget(console);
    window.addDockWidget(Qt::BottomDockWidgetArea,consoleDock);
    window.resizeDocks({consoleDock},{148},Qt::Vertical);
    window.resizeDocks({toolsDock,inspectorDock},{200,370},Qt::Horizontal);
    window.resizeDocks({inspectorDock,graphicsDock},{330,380},Qt::Vertical);
    QObject::connect(viewport,&Renderer::statusText,console,[console](const QString& message){console->append(message);});
    QObject::connect(hierarchy,&HierarchyWidget::statusText,console,[console](const QString& message){console->append(message);});
    QObject::connect(viewport,&Renderer::statusText,window.statusBar(),[&window](const QString& message){window.statusBar()->showMessage(message,6000);});
    window.statusBar()->showMessage("EngineLabs  |  Click viewport, hold RMB to look and use WASD to fly");
    window.show();
    return app.exec();
}
