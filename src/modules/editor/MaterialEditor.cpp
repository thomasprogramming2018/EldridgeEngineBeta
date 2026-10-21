#include "MaterialEditor.h"
#include "ShaderEditorDialog.h"
#include <QDir>
#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <algorithm>

namespace {
QDoubleSpinBox* makeNumber(QWidget* parent, double value, double low=-1000, double high=1000)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(low, high);
    spin->setDecimals(3);
    spin->setSingleStep(0.05);
    spin->setValue(value);
    return spin;
}
QColor valueColor(const MaterialValue& value)
{
    if (auto* v = std::get_if<QVector3D>(&value))
        return QColor::fromRgbF(std::clamp(double(v->x()),0.,1.),
                                std::clamp(double(v->y()),0.,1.),
                                std::clamp(double(v->z()),0.,1.));
    if (auto* v = std::get_if<QVector4D>(&value))
        return QColor::fromRgbF(std::clamp(double(v->x()),0.,1.),
                                std::clamp(double(v->y()),0.,1.),
                                std::clamp(double(v->z()),0.,1.),
                                std::clamp(double(v->w()),0.,1.));
    return Qt::white;
}
}

MaterialEditor::MaterialEditor(std::shared_ptr<Material> material, QWidget* parent)
    : QWidget(parent), material_(std::move(material))
{
    root_ = new QVBoxLayout(this);
    root_->setContentsMargins(10,10,10,10);
    root_->setSpacing(10);
    rebuild();
}
void MaterialEditor::rebuild()
{
    while (auto* item = root_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    auto* heading = new QLabel("MATERIAL  ·  REFLECTED PROPERTIES", this);
    heading->setObjectName("sectionTitle");
    root_->addWidget(heading);

    auto* name = new QLineEdit(material_->name, this);
    name->setPlaceholderText("Material name");
    root_->addWidget(name);
    connect(name, &QLineEdit::textChanged, this, [this](const QString& text) {
        material_->name=text;
        emit materialChanged();
    });

    auto* shaderbox = new QComboBox(this);
    shaderbox->addItem("Choose shader...", QString());
    QDir directory(ENGINE_SHADER_DIRECTORY);
    for (const QString& file : directory.entryList({"*.elsha"}, QDir::Files))
        shaderbox->addItem(QFileInfo(file).completeBaseName(), directory.absoluteFilePath(file));

    QString currentPath;
    if (material_->shader)
        currentPath = material_->shader->assetPath();
    int currentIndex = shaderbox->findData(currentPath);
    if (!currentPath.isEmpty() && currentIndex < 0)
    {
        shaderbox->addItem(QFileInfo(currentPath).completeBaseName(), currentPath);
        currentIndex = shaderbox->count() - 1;
    }
    shaderbox->setCurrentIndex(currentIndex < 0 ? 0 : currentIndex);
    root_->addWidget(shaderbox);
    connect(shaderbox, &QComboBox::activated, this, [this, shaderbox](int index)
    {
        QString path = shaderbox->itemData(index).toString();
        if (!path.isEmpty())
            emit customShaderRequested(material_, path);
    });

    auto* open = new QPushButton("Open .elsha Shader", this);
    auto* create = new QPushButton("New Shader", this);
    auto* edit = new QPushButton("Edit Shader", this);
    edit->setEnabled(!currentPath.isEmpty());
    root_->addWidget(open);
    root_->addWidget(create);
    root_->addWidget(edit);
    connect(open, &QPushButton::clicked, this, [this]()
    {
        QString path = QFileDialog::getOpenFileName(this, "Open Shader", QString(),
                                                  "Eldridge Shader (*.elsha)");
        if (!path.isEmpty())
            emit customShaderRequested(material_, path);
    });
    connect(create, &QPushButton::clicked, this, [this]()
    {
        ShaderEditorDialog dialog(QString(), this);
        if (dialog.exec() == QDialog::Accepted)
            emit customShaderRequested(material_, dialog.savedPath());
    });
    connect(edit, &QPushButton::clicked, this, [this, currentPath]()
    {
        ShaderEditorDialog dialog(currentPath, this);
        if (dialog.exec() == QDialog::Accepted)
            emit customShaderRequested(material_, dialog.savedPath());
    });

    if (!material_->shader) {
        auto* missing = new QLabel("Shader not loaded.\nCheck the Console for compilation errors.",this);
        missing->setWordWrap(true);
        root_->addWidget(missing);
        root_->addStretch();
        return;
    }
    int count = 0;
    for (const auto& u : material_->shader->uniforms())
        if (Material::isEditableUniform(u)) ++count;
    auto* subtitle = new QLabel(QString("%1 editable uniforms discovered at link time").arg(count),this);
    subtitle->setObjectName("mutedLabel");
    subtitle->setWordWrap(true);
    root_->addWidget(subtitle);

    auto* frame = new QWidget(this);
    auto* form = new QFormLayout(frame);
    form->setSpacing(12);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    for (const auto& u : material_->shader->uniforms()) {
        if (!Material::isEditableUniform(u)) continue;
        if (QWidget* widget = makeControl(u)) {
            auto* label = new QLabel(u.name,frame);
            label->setMinimumWidth(84);
            label->setToolTip(QString("OpenGL type: %1 • location: %2").arg(u.type).arg(u.location));
            form->addRow(label,widget);
        }
    }
    root_->addWidget(frame);
    root_->addStretch();
}
QWidget* MaterialEditor::makeVector(const ReflectedUniform& uniform, int dimensions)
{
    auto* row = new QWidget(this);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(4);
    const auto* stored = material_->get(uniform.name);
    if (!stored) { delete row; return nullptr; }
    for (int i=0;i<dimensions;++i) {
        double current=0;
        if (dimensions==2) current=std::get<QVector2D>(*stored)[i];
        if (dimensions==3) current=std::get<QVector3D>(*stored)[i];
        if (dimensions==4) current=std::get<QVector4D>(*stored)[i];
        auto* spin=makeNumber(row,current);
        spin->setPrefix(QString("%1 ").arg("XYZW"[i]));
        spin->setMinimumWidth(55);
        layout->addWidget(spin);
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this,name=uniform.name,dimensions,i](double value) {
            const auto* current = material_->get(name);
            if (!current) return;
            if (dimensions==2) {
                auto v=std::get<QVector2D>(*current); v[i]=float(value); material_->set(name,v);
            } else if (dimensions==3) {
                auto v=std::get<QVector3D>(*current); v[i]=float(value); material_->set(name,v);
            } else {
                auto v=std::get<QVector4D>(*current); v[i]=float(value); material_->set(name,v);
            }
            emit materialChanged();
        });
    }
    return row;
}
QWidget* MaterialEditor::makeMatrix(const ReflectedUniform& uniform)
{
    const auto* stored=material_->get(uniform.name);
    if (!stored) return nullptr;
    auto* panel=new QWidget(this);
    auto* grid=new QGridLayout(panel);
    grid->setContentsMargins(0,0,0,0);
    const auto initial=std::get<QMatrix4x4>(*stored);
    for (int r=0;r<4;++r) for (int c=0;c<4;++c) {
        auto* spin=makeNumber(panel,initial(r,c));
        spin->setMinimumWidth(45);
        grid->addWidget(spin,r,c);
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,
                [this,name=uniform.name,r,c](double value) {
            const auto* stored=material_->get(name);
            if (!stored) return;
            auto matrix=std::get<QMatrix4x4>(*stored);
            matrix(r,c)=float(value);
            material_->set(name,matrix);
            emit materialChanged();
        });
    }
    return panel;
}
QWidget* MaterialEditor::makeControl(const ReflectedUniform& uniform)
{
    const QString name = uniform.name;
    const auto* stored=material_->get(name);
    if (!stored) return nullptr;
    const EditorHint hint = material_->shader->editorHint(name);
    if (hint==EditorHint::Color &&
        (uniform.type==GL_FLOAT_VEC3 || uniform.type==GL_FLOAT_VEC4)) {
        auto* button=new QPushButton(this);
        auto refresh=[this,name,button]() {
            const auto* value=material_->get(name);
            if (!value) return;
            QColor color=valueColor(*value);
            button->setText(color.name(QColor::HexArgb));
            button->setStyleSheet(QString("QPushButton { background: %1; color: %2; border: 1px solid #6b7486; border-radius: 4px; padding: 6px; }")
                                  .arg(color.name(),color.lightness()<140 ? "white" : "#111827"));
        };
        refresh();
        connect(button,&QPushButton::clicked,this,[this,name,refresh,uniform]() {
            const auto* existing=material_->get(name);
            if (!existing) return;
            QColorDialog::ColorDialogOptions options;
            if (uniform.type==GL_FLOAT_VEC4) options |= QColorDialog::ShowAlphaChannel;
            const QColor chosen=QColorDialog::getColor(valueColor(*existing),this,name,options);
            if (!chosen.isValid()) return;
            if (uniform.type==GL_FLOAT_VEC3)
                material_->set(name,QVector3D(chosen.redF(),chosen.greenF(),chosen.blueF()));
            else
                material_->set(name,QVector4D(chosen.redF(),chosen.greenF(),chosen.blueF(),chosen.alphaF()));
            refresh();
            emit materialChanged();
        });
        return button;
    }
    switch (uniform.type) {
    case GL_FLOAT: {
        const bool slider=hint==EditorHint::Slider;
        auto* spin=makeNumber(this,std::get<float>(*stored),slider?0:-1000,slider?1:1000);
        if (slider) spin->setSingleStep(0.01);
        connect(spin,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,
                [this,name](double v){material_->set(name,float(v));emit materialChanged();});
        return spin;
    }
    case GL_INT: {
        auto* spin=new QSpinBox(this);
        spin->setRange(-1000,1000);
        spin->setValue(std::get<int>(*stored));
        connect(spin,QOverload<int>::of(&QSpinBox::valueChanged),this,
                [this,name](int v){material_->set(name,v);emit materialChanged();});
        return spin;
    }
    case GL_BOOL: {
        auto* box=new QCheckBox("Enabled",this);
        box->setChecked(std::get<bool>(*stored));
        connect(box,&QCheckBox::toggled,this,[this,name](bool v){material_->set(name,v);emit materialChanged();});
        return box;
    }
    case GL_FLOAT_VEC2: return makeVector(uniform,2);
    case GL_FLOAT_VEC3: return makeVector(uniform,3);
    case GL_FLOAT_VEC4: return makeVector(uniform,4);
    case GL_FLOAT_MAT4: return makeMatrix(uniform);
    case GL_SAMPLER_2D: {
        auto* row=new QWidget(this);
        auto* layout=new QHBoxLayout(row);
        layout->setContentsMargins(0,0,0,0);
        auto* pathLabel=new QLineEdit(row);
        pathLabel->setReadOnly(true);
        const auto path=std::get<TexturePath>(*stored).path;
        pathLabel->setText(path.isEmpty()? "No image" : QFileInfo(path).fileName());
        pathLabel->setToolTip(path);
        auto* browse=new QPushButton("…",row);
        browse->setToolTip("Choose texture image");
        auto* clear=new QPushButton("×",row);
        clear->setToolTip("Clear texture");
        layout->addWidget(pathLabel,1);
        layout->addWidget(browse);
        layout->addWidget(clear);
        connect(browse,&QPushButton::clicked,this,[this,name,pathLabel]() {
            const auto path=QFileDialog::getOpenFileName(this,"Choose texture",QString(),
                               "Image files (*.png *.jpg *.jpeg *.bmp *.tga *.webp)");
            if (path.isEmpty()) return;
            material_->set(name,TexturePath{path});
            pathLabel->setText(QFileInfo(path).fileName());
            pathLabel->setToolTip(path);
            if (material_->parameters.contains("uUseTexture")) material_->set("uUseTexture",true);
            emit materialChanged();
            QTimer::singleShot(0,this,[this](){rebuild();});
        });
        connect(clear,&QPushButton::clicked,this,[this,name,pathLabel]() {
            material_->set(name,TexturePath{});
            pathLabel->setText("No image");
            if (material_->parameters.contains("uUseTexture")) material_->set("uUseTexture",false);
            emit materialChanged();
            QTimer::singleShot(0,this,[this](){rebuild();});
        });
        return row;
    }
    default: return nullptr;
    }
}
