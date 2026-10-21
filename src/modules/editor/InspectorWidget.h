#pragma once
#include <QWidget>
#include "../material/Material.h"
#include "../componentsBackend/TransformComponent/TransformComponent.h"

#include "TransformWidget.h"


class SceneObject;
class TerrainComponent;
class QVBoxLayout;

class InspectorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit InspectorWidget(QWidget* parent = nullptr);

    void setObject(SceneObject* object);
    void refresh();

signals:
    void materialChanged();
    void transformChanged();
    void terrainChanged();
    void lightChanged();
    void frameTerrainRequested(TerrainComponent* terrain);
    void terrainBrushRequested(TerrainComponent* terrain,int mode,float radius,float strength,int layer,float flatHeight);

    void shaderChangeRequested(
        std::shared_ptr<Material> material,
        const QString& shaderName
    );

    void customShaderRequested(std::shared_ptr<Material> material, const QString& path);

private:
    SceneObject* selected_ = nullptr;
    QVBoxLayout* layout_ = nullptr;
};
