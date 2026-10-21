#include "MeshComponentWidget.h"

#include "../componentsBackend/MeshComponent/MeshComponent.h"

#include <QLabel>
#include <QVBoxLayout>

MeshComponentWidget::MeshComponentWidget(const MeshComponent& component, QWidget* parent)
    : QGroupBox("Mesh Component", parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(component.mesh ? "Mesh loaded" : "No mesh", this));
}
