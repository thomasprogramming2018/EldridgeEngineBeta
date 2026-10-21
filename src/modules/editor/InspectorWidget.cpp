#include "InspectorWidget.h"
#include "LightWidget.h"
#include "../componentsBackend/LightComponent/LightComponent.h"
#include "MaterialEditor.h"
#include "TerrainWidget.h"
#include "../componentsBackend/TerrainComponent/TerrainComponent.h"
#include "MeshComponentWidget.h"

#include "../scene/SceneObject.h"
#include "../material/MaterialComponent.h"
#include "../componentsBackend/MeshComponent/MeshComponent.h"

#include <QVBoxLayout>
#include <QGroupBox>

InspectorWidget::InspectorWidget(QWidget* parent)
    : QWidget(parent)
{
    layout_ = new QVBoxLayout(this);
}

void InspectorWidget::setObject(SceneObject* object)
{
    selected_ = object;

    
    refresh();
}

void InspectorWidget::refresh()
{
    while (auto* item = layout_->takeAt(0))
    {
        delete item->widget();
        delete item;
    }

    if (!selected_)
        return;

    for (const auto& owned : selected_->components())
    {
        Component* component = owned.get();

        if (auto* light = dynamic_cast<LightComponent*>(component))
        {
            auto* editor = new LightWidget(*light, this);
            connect(editor, &LightWidget::lightChanged, this, &InspectorWidget::lightChanged);
            layout_->addWidget(editor);
        }
        else if (auto* terrain = dynamic_cast<TerrainComponent*>(component))
        {
            auto* editor = new TerrainWidget(*terrain, this);
            connect(editor, &TerrainWidget::frameRequested, this, &InspectorWidget::frameTerrainRequested);
            connect(editor, &TerrainWidget::terrainChanged, this, &InspectorWidget::terrainChanged);
            connect(editor, &TerrainWidget::brushRequested, this, &InspectorWidget::terrainBrushRequested);
            layout_->addWidget(editor);
        }
        else if (auto* mesh = dynamic_cast<MeshComponent*>(component))
        {
            layout_->addWidget(new MeshComponentWidget(*mesh, this));
        }
        else if (auto* material = dynamic_cast<MaterialComponent*>(component))
        {
            if (!material->material)
                continue;

            auto* group = new QGroupBox("Material Component", this);
            auto* groupLayout = new QVBoxLayout(group);

            auto* editor = new MaterialEditor(material->material, group);
            groupLayout->addWidget(editor);

            connect(editor, &MaterialEditor::materialChanged,
                    this, &InspectorWidget::materialChanged);

            connect(
                editor,
                &MaterialEditor::shaderChangeRequested,
                this,
                &InspectorWidget::shaderChangeRequested
            );

            

            connect(editor, &MaterialEditor::customShaderRequested,
                    this, &InspectorWidget::customShaderRequested);
            layout_->addWidget(group);
        }
        if (auto* transform = dynamic_cast<TransformComponent*>(component))
        {
            auto* editor = new TransformWidget(*transform, this);
            connect(editor, &TransformWidget::transformChanged,
                    this, &InspectorWidget::transformChanged);
            layout_->addWidget(editor);
        }
    }

    layout_->addStretch();
}
