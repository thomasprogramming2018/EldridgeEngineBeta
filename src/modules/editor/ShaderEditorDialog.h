#pragma once

#include <QDialog>
#include "../shader/ShaderAsset.h"

class QLineEdit;
class QPlainTextEdit;
class QCheckBox;

class ShaderEditorDialog : public QDialog
{
public:
    explicit ShaderEditorDialog(const QString& path, QWidget* parent = nullptr);
    QString savedPath() const { return path_; }

private:
    void saveShader();

    ShaderAsset asset_;
    QString path_;
    QLineEdit* name_;
    QPlainTextEdit* vertex_;
    QPlainTextEdit* fragment_;
    QCheckBox* lighting_;
};
