#include "TransformWidget.h"
#include "../componentsBackend/TransformComponent/TransformComponent.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

TransformWidget::TransformWidget(TransformComponent& transform, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* group = new QGroupBox("Transform", this);
    auto* form = new QFormLayout(group);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    form->addRow("Position", makeVectorRow(transform.position, 0.1));
    form->addRow("Rotation (degrees)", makeVectorRow(transform.rotation, 1.0));
    form->addRow("Scale", makeVectorRow(transform.scale, 0.1));

    layout->addWidget(group);
}

QWidget* TransformWidget::makeVectorRow(QVector3D& value, double step)
{
    auto* row = new QWidget(this);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    const QString axes[] = {"X", "Y", "Z"};
    for (int axis = 0; axis < 3; ++axis)
    {
        auto* spin = new QDoubleSpinBox(row);
        spin->setRange(-100000.0, 100000.0);
        spin->setDecimals(3);
        spin->setSingleStep(step);
        spin->setPrefix(axes[axis] + " ");
        spin->setMinimumWidth(70);

        // QVector3D uses index 0 for X, 1 for Y, and 2 for Z.
        spin->setValue(value[axis]);
        layout->addWidget(spin);

        connect(spin, &QDoubleSpinBox::valueChanged,
                this, [this, &value, axis](double number)
        {
            value[axis] = float(number);
            emit transformChanged();
        });
    }

    return row;
}
