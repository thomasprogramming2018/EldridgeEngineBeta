#include "TerrainWidget.h"
#include "../componentsBackend/TerrainComponent/TerrainComponent.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

TerrainWidget::TerrainWidget(TerrainComponent& terrain,QWidget* parent) : QWidget(parent)
{
    auto* layout=new QVBoxLayout(this);
    auto* group=new QGroupBox("Terrain",this); auto* form=new QFormLayout(group); layout->addWidget(group);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto number=[&](const QString& name,double value,double minimum,double maximum,double step)
    { auto* control=new QDoubleSpinBox(group); control->setRange(minimum,maximum); control->setValue(value); control->setSingleStep(step); form->addRow(name,control); return control; };
    auto* resolution=new QComboBox(group);
    for(int count:{33,65,129,257,513}) resolution->addItem(QString::number(count),count);
    resolution->setCurrentText(QString::number(terrain.resolution)); form->addRow("Resolution",resolution);
    auto* size=number("World size",terrain.size,1,10000,1);
    auto* range=number("Height range",terrain.heightRange,0.01,10000,1);
    auto* offset=number("Height offset",terrain.heightOffset,-10000,10000,1);
    auto* tiling=number("Layer tiling",terrain.tiling,0.1,1000,1);
    connect(range,&QDoubleSpinBox::valueChanged,this,[this,&terrain](double v){terrain.heightRange=v;emit terrainChanged();});
    connect(offset,&QDoubleSpinBox::valueChanged,this,[this,&terrain](double v){terrain.heightOffset=v;emit terrainChanged();});
    connect(tiling,&QDoubleSpinBox::valueChanged,this,[this,&terrain](double v){terrain.tiling=v;++terrain.revision;emit terrainChanged();});
    auto* frame=new QPushButton("Frame Terrain",group);form->addRow(frame);
    connect(frame,&QPushButton::clicked,this,[this,&terrain](){emit frameRequested(&terrain);});
    auto* reset=new QPushButton("Apply size / resolution (reset heights)",group); form->addRow(reset);
    connect(reset,&QPushButton::clicked,this,[=,&terrain]()
    {
        if(QMessageBox::question(this,"Reset Terrain","Reset the heightfield to apply the new size and resolution?",QMessageBox::Yes|QMessageBox::Cancel)!=QMessageBox::Yes) return;
        terrain.resize(resolution->currentData().toInt(),size->value()); emit terrainChanged();
    });
    auto* seed=new QSpinBox(group); seed->setRange(0,1000000); seed->setValue(1); form->addRow("Noise seed",seed);
    auto* amplitude=number("Noise amplitude",6,0,1000,1); auto* frequency=number("Noise frequency",4,0.1,100,0.5);
    auto* generate=new QPushButton("Generate hills",group); form->addRow(generate);
    connect(generate,&QPushButton::clicked,this,[=,&terrain]() {terrain.generate(seed->value(),amplitude->value(),frequency->value());emit terrainChanged();});
    auto* erode=new QPushButton("Thermal erosion (20 steps)",group); form->addRow(erode);
    connect(erode,&QPushButton::clicked,this,[this,&terrain]() {terrain.erode(20,terrain.size/terrain.resolution*0.5f);emit terrainChanged();});
    auto file=[&](const QString& label,bool save,bool weights)
    {
        auto* button=new QPushButton(label,group); form->addRow(button);
        connect(button,&QPushButton::clicked,this,[this,&terrain,save,weights]()
        {
            QString path=save?QFileDialog::getSaveFileName(this,"Export Terrain",weights?"Weights.png":"Height.png","PNG (*.png)"):
                QFileDialog::getOpenFileName(this,"Import Terrain",QString(),"Images (*.png *.jpg *.bmp *.tif)");
            if(path.isEmpty()) return; QString error; bool ok;
            if(weights) ok=save?terrain.exportWeights(path,error):terrain.importWeights(path,error);
            else ok=save?terrain.exportHeightmap(path,error):terrain.importHeightmap(path,error);
            if(!ok) QMessageBox::warning(this,"Terrain",error); else if(!save) emit terrainChanged();
        });
    };
    file("Import 16-bit heightmap...",false,false); file("Export 16-bit heightmap...",true,false);
    file("Import RGBA weight map...",false,true); file("Export RGBA weight map...",true,true);
    for(int layer=0;layer<4;++layer)
    {
        auto* texture=new QPushButton(QString("Layer %1 texture...").arg(layer+1),group);
        texture->setToolTip(terrain.layers[layer].texture); form->addRow(texture);
        connect(texture,&QPushButton::clicked,this,[this,&terrain,layer,texture]()
        { QString path=QFileDialog::getOpenFileName(this,"Terrain Layer",QString(),"Images (*.png *.jpg *.bmp)");
          if(!path.isEmpty()) {terrain.layers[layer].texture=path;texture->setToolTip(path);++terrain.revision;emit terrainChanged();} });
        auto* color=new QPushButton(QString("Layer %1 tint...").arg(layer+1),group); form->addRow(color);
        connect(color,&QPushButton::clicked,this,[this,&terrain,layer]()
        {
            auto v=terrain.layers[layer].color; auto chosen=QColorDialog::getColor(QColor::fromRgbF(v.x(),v.y(),v.z()),this);
            if(chosen.isValid()) {terrain.layers[layer].color={float(chosen.redF()),float(chosen.greenF()),float(chosen.blueF())};++terrain.revision;emit terrainChanged();}
        });
    }
    auto* enabled=new QCheckBox("Edit terrain in viewport (left drag)",group); form->addRow(enabled);
    auto* mode=new QComboBox(group); mode->addItems({"Raise","Lower","Smooth","Flatten","Paint layer"}); form->addRow("Brush",mode);
    auto* radius=number("Brush radius",4,0.1,1000,0.5);
    auto* strength=number("Brush strength",0.3,0.001,20,0.05);
    auto* flat=number("Flatten height",0,-10000,10000,0.5);
    auto* layer=new QComboBox(group); layer->addItems({"1 — Grass","2 — Dirt","3 — Rock","4 — Snow"}); form->addRow("Paint layer",layer);
    auto update=[=,&terrain]() {emit brushRequested(enabled->isChecked()?&terrain:nullptr,mode->currentIndex(),radius->value(),strength->value(),layer->currentIndex(),flat->value());};
    connect(enabled,&QCheckBox::toggled,this,update); connect(mode,&QComboBox::currentIndexChanged,this,update);
    connect(radius,&QDoubleSpinBox::valueChanged,this,update); connect(strength,&QDoubleSpinBox::valueChanged,this,update);
    connect(flat,&QDoubleSpinBox::valueChanged,this,update); connect(layer,&QComboBox::currentIndexChanged,this,update);
}
