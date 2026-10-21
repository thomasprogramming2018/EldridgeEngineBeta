#include "ObjectCreationDialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

ObjectCreationDialog::ObjectCreationDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle("Create Object");
    auto* form = new QFormLayout(this);
    name_ = new QLineEdit("Object", this);
    type_ = new QComboBox(this);
    type_->addItems({"Cube", "OBJ Mesh", "Terrain", "Empty", "Directional Light", "Point Light", "Spot Light", "Rectangle Light"});
    shader_ = new QComboBox(this);
    shader_->addItems({"PBR", "HDRP", "Lit", "Unlit"});
    connect(type_,&QComboBox::currentTextChanged,this,[this](const QString& type)
    {shader_->setEnabled(type=="Cube"||type=="OBJ Mesh");});
    shader_->setItemData(1, "Eldridge HDR material with clear coat", Qt::ToolTipRole);
    path_ = new QLineEdit(this);
    auto* browse = new QPushButton("Choose OBJ...", this);
    form->addRow("Name", name_);
    form->addRow("Object", type_);
    form->addRow("Material", shader_);
    form->addRow("OBJ file", path_);
    form->addRow(browse);
    connect(browse, &QPushButton::clicked, this, [this]()
    {
        QString path = QFileDialog::getOpenFileName(this, "Import OBJ", QString(), "OBJ Mesh (*.obj)");
        if (!path.isEmpty()) { path_->setText(path); type_->setCurrentText("OBJ Mesh"); }
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]()
    {
        if (name_->text().trimmed().isEmpty() || (objectType() == "OBJ Mesh" && meshPath().isEmpty()))
        { QMessageBox::warning(this, "Object", "Enter a name and choose an OBJ file when importing."); return; }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
QString ObjectCreationDialog::objectName() const { return name_->text().trimmed(); }
QString ObjectCreationDialog::objectType() const { return type_->currentText(); }
QString ObjectCreationDialog::shaderName() const { return shader_->currentText(); }
QString ObjectCreationDialog::meshPath() const { return path_->text().trimmed(); }
