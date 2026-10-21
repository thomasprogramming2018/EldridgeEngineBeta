#include "ShaderEditorDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

ShaderEditorDialog::ShaderEditorDialog(const QString& path, QWidget* parent)
    : QDialog(parent), path_(path)
{
    setWindowTitle("Shader Editor (.elsha)");
    resize(850, 650);

    if (!path.isEmpty() && !asset_.load(path))
    {
        QMessageBox::warning(this, "Shader", "Cannot read this .elsha file.");
        return;
    }

    if (path.isEmpty())
    {
        // Start new shaders with the working Lit shader from disk.
        asset_.load(QString(ENGINE_SHADER_DIRECTORY) + "/Lit.elsha");
        asset_.name = "New Shader";
    }

    auto* layout = new QVBoxLayout(this);
    name_ = new QLineEdit(asset_.name, this);
    layout->addWidget(name_);

    lighting_ = new QCheckBox("Affected by lights", this);
    lighting_->setChecked(asset_.affectedByLights);
    layout->addWidget(lighting_);

    auto* help = new QLabel(
        "The engine supplies uAffectedByLights, uTime, camera and lighting uniforms.\n"
        "Use uAffectedByLights in your fragment shader to choose lit or unlit output.\n"
        "Lighting uniforms are hidden from material controls.", this);
    help->setWordWrap(true);
    layout->addWidget(help);

    auto* tabs = new QTabWidget(this);
    vertex_ = new QPlainTextEdit(asset_.vertexSource, tabs);
    fragment_ = new QPlainTextEdit(asset_.fragmentSource, tabs);
    tabs->addTab(vertex_, "Vertex");
    tabs->addTab(fragment_, "Fragment");
    layout->addWidget(tabs);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* save = buttons->addButton("Save and Apply", QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(save, &QPushButton::clicked, this, &ShaderEditorDialog::saveShader);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ShaderEditorDialog::saveShader()
{
    if (name_->text().trimmed().isEmpty() || vertex_->toPlainText().trimmed().isEmpty() ||
        fragment_->toPlainText().trimmed().isEmpty())
    {
        QMessageBox::warning(this, "Shader", "Enter a name and both shader sources.");
        return;
    }

    QString path = path_;
    if (path.isEmpty())
        path = QFileDialog::getSaveFileName(this, "Save Shader", "NewShader.elsha",
                                          "Eldridge Shader (*.elsha)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".elsha"))
        path += ".elsha";

    asset_.name = name_->text().trimmed();
    asset_.vertexSource = vertex_->toPlainText();
    asset_.fragmentSource = fragment_->toPlainText();
    asset_.affectedByLights = lighting_->isChecked();
    if (!asset_.save(path))
    {
        QMessageBox::warning(this, "Shader", "Could not save the shader file.");
        return;
    }
    path_ = path;
    accept();
}
