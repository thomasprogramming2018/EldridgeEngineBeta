#pragma once
#include <QDialog>
class QLineEdit;
class QComboBox;

class ObjectCreationDialog : public QDialog
{
public:
    explicit ObjectCreationDialog(QWidget* parent = nullptr);
    QString objectName() const;
    QString objectType() const;
    QString shaderName() const;
    QString meshPath() const;
private:
    QLineEdit* name_;
    QComboBox* type_;
    QComboBox* shader_;
    QLineEdit* path_;
};
