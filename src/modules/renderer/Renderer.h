#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QElapsedTimer>
#include <QSet>
#include <QPoint>
#include <QVector3D>
#include <memory>
#include "GPUMesh.h"
#include "../editor/TransformGizmo.h"
#include "PostProcessor.h"
#include "ShadowMap.h"
#include "HardwareViewport.h"
#include <map>
#include "../resources/ResourceManager.h"
#include "../material/Material.h"
#include "../material/MaterialComponent.h"
#include "camera.h"
#include <QElapsedTimer>

class Scene;
class SceneObject;
class TerrainComponent;
class QPainter;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QFocusEvent;
class QTimerEvent;

// Scene viewport: owns OpenGL resources; Inspector edits CPU component values.
class Renderer final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit Renderer(Scene& scene, QWidget* parent = nullptr);
    ~Renderer() override;
    std::shared_ptr<Material> material() const { return material_; }
    void selectShader(const QString& key);
    void focusCube();
    void focusObject(SceneObject* object);
    void resetCamera();
    void setSelectedObject(SceneObject* object);
    void setGizmoMode(int mode);
    void setGizmoLocal(bool local) { gizmo_.local=local; update(); }
    void setGizmoSnap(bool snap) { gizmo_.snap=snap; update(); }
    void invalidateLighting() { raySceneDirty_=true; shadowDirty_=true; update(); }

    void changeMaterialShader(
    std::shared_ptr<Material> material,
    const QString& shaderName);

    


    void setScene(Scene& scene);
    void invalidateScene() { sceneDirty_ = true; update(); }
    void setTerrainBrush(TerrainComponent* terrain,int mode,float radius,float strength,int layer,float flatHeight);
    void loadMaterialShader(std::shared_ptr<Material> material, const QString& path);

    QElapsedTimer timeClock_;

    Camera camera;
    RenderSettings renderSettings;
    const RayLightingFrame& hardwareLightingFrame() const { return latestRayFrame_; }
signals:
    void objectPicked(SceneObject* object);
    void transformEdited();
    void gizmoModeChanged(int mode);
    void shaderChanged();
    void sceneResourcesReady();
    void hardwareFrameReady(int percent);
    void hardwareRayTracingFailed(const QString& error);
    void statusText(const QString& message);
protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void timerEvent(QTimerEvent* event) override;

    
    

private:
    TransformGizmo gizmo_;
    void drawEditorTools(QPainter& painter);
    SceneObject* pickObject(const QPoint& position);
    void releaseGL();
    void moveCamera(float seconds);
    void updateHardwareLighting();
    void drawRenderStats(QPainter& painter);
    void updateTerrainBrush(const QPoint& position,bool apply);
    void uploadMaterialUniforms(
        const Shader& shader,
        const Material& material
    );
    Scene* scene_ = nullptr;
    QMetaObject::Connection sceneChangedConnection_;
    bool sceneDirty_ = true;
    QMetaObject::Connection sceneResetConnection_;
    struct TerrainGpu { std::unique_ptr<QOpenGLTexture> weights; unsigned int revision=0; };
    std::map<const TerrainComponent*,TerrainGpu> terrains_;
    TerrainComponent* brushTerrain_ = nullptr;
    int brushMode_ = 0, brushLayer_ = 0;
    float brushRadius_ = 4, brushStrength_ = 0.3f, brushFlat_ = 0;
    bool brushStroke_ = false, brushHitValid_ = false;
    QVector3D brushHit_;
    std::unique_ptr<HardwareViewport> hardware_;
    QMetaObject::Connection historyConnection_;
    std::shared_ptr<RaySceneSnapshot> rayScene_;
    bool raySceneDirty_=true;
    RayLightingFrame latestRayFrame_;
    int adaptiveRayWidth_ = 0;
    int slowRayFrames_ = 0, fastRayFrames_ = 0;
    QElapsedTimer fpsClock_;
    int fpsFrames_ = 0;
    float rasterFps_ = 0;
    QMatrix4x4 rayCamera_;
    QMatrix4x4 motionCamera_;
    QElapsedTimer rayMotionClock_;
    QJsonObject raySettings_;
    int rayProgress_=0;
    QSize raySize_;
    ResourceManager resources_;
    std::map<const Mesh*, std::unique_ptr<GPUMesh>> meshes_;
    PostProcessor post_;
    ShadowMap shadows_;
    bool shadowsReady_=false, shadowDirty_=true;
    bool postReady_ = false;
    std::shared_ptr<Material> material_;
    QString shaderKey_ = "PBR";
    bool ready_ = false;
    bool glInitialized_ = false;
    int timerId_ = 0;
    QElapsedTimer frameClock_;
    QSet<int> keys_;
    QPoint lastMouse_;
    bool looking_ = false;
    float speed_ = 3.0f;
};
