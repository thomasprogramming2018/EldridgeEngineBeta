#include "LightWidget.h"
#include "../componentsBackend/LightComponent/LightComponent.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
LightWidget::LightWidget(LightComponent& light, QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
    auto* group = new QGroupBox("Light", this); layout->addWidget(group);
    auto* form = new QFormLayout(group);
    auto* type = new QComboBox(group); type->addItems({"Directional", "Point", "Spot", "Rectangle"});
    type->setCurrentIndex(int(light.type)); form->addRow("Type", type);
    auto* enabled = new QCheckBox(group); enabled->setChecked(light.enabled); form->addRow("Enabled", enabled);
    auto* shadows = new QCheckBox(group); shadows->setChecked(light.shadows); form->addRow("Ray-traced shadows", shadows);
    auto* color = new QPushButton("Choose color...", group); form->addRow("Color", color);
    auto number = [&](const QString& name, float& value, double low, double high, double step)
    {
        auto* spin = new QDoubleSpinBox(group); spin->setRange(low,high); spin->setDecimals(3); spin->setSingleStep(step); spin->setValue(value);
        form->addRow(name, spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, &value](double v) {value=float(v);emit lightChanged();});
        return spin;
    };
    number("Intensity (engine units)", light.intensity, 0, 100000, 1);
    auto* range = number("Range", light.range, 0.01, 100000, 1);
    auto* radius = number("Source radius", light.radius, 0, 1000, 0.05);
    auto* angle = number("Source angle (degrees)", light.angle, 0, 20, 0.1);
    auto* width = number("Width", light.width, 0.01, 10000, 0.1);
    auto* height = number("Height", light.height, 0.01, 10000, 0.1);
    auto* inner = new QDoubleSpinBox(group); inner->setRange(0,89); inner->setValue(light.innerCone); form->addRow("Inner cone (degrees)",inner);
    auto* outer = new QDoubleSpinBox(group); outer->setRange(0.1,89.9); outer->setValue(light.outerCone); form->addRow("Outer cone (degrees)",outer);
    connect(inner, &QDoubleSpinBox::valueChanged, this, [this,&light,outer](double value)
    { light.innerCone=value; if(light.outerCone<value){light.outerCone=value;QSignalBlocker block(outer);outer->setValue(value);} emit lightChanged(); });
    connect(outer, &QDoubleSpinBox::valueChanged, this, [this,&light,inner](double value)
    { light.outerCone=value; if(light.innerCone>value){light.innerCone=value;QSignalBlocker block(inner);inner->setValue(value);} emit lightChanged(); });
    auto refresh = [=,&light]()
    {
        bool directional=light.type==LightComponent::Type::Directional, spot=light.type==LightComponent::Type::Spot, rect=light.type==LightComponent::Type::Rectangle;
        range->setEnabled(!directional); radius->setEnabled(!directional&&!rect); angle->setEnabled(directional);
        width->setEnabled(rect);height->setEnabled(rect);inner->setEnabled(spot);outer->setEnabled(spot);
        QColor tint=QColor::fromRgbF(light.color.x(),light.color.y(),light.color.z());
        color->setStyleSheet("background-color:"+tint.name()+";");
    };
    connect(type,&QComboBox::currentIndexChanged,this,[this,&light,refresh](int value){light.type=LightComponent::Type(value);refresh();emit lightChanged();});
    connect(enabled,&QCheckBox::toggled,this,[this,&light](bool value){light.enabled=value;emit lightChanged();});
    connect(shadows,&QCheckBox::toggled,this,[this,&light](bool value){light.shadows=value;emit lightChanged();});
    connect(color,&QPushButton::clicked,this,[this,&light,refresh]()
    {auto c=QColorDialog::getColor(QColor::fromRgbF(light.color.x(),light.color.y(),light.color.z()),this,"Light color");if(c.isValid()){light.color={float(c.redF()),float(c.greenF()),float(c.blueF())};refresh();emit lightChanged();}});
    refresh();
}
