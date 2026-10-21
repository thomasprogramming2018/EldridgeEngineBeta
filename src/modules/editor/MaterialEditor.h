#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <memory>
#include <QComboBox>

#include "../material/Material.h"

// Pure Qt UI: edits material CPU values; never makes GL calls.
class MaterialEditor : public QWidget {
    Q_OBJECT
public:
    explicit MaterialEditor(std::shared_ptr<Material> material, QWidget* parent = nullptr);
    void rebuild();
signals:
    void materialChanged();

    void shaderChangeRequested(
        std::shared_ptr<Material> material,
        const QString& shaderName
    );

    void customShaderRequested(
        std::shared_ptr<Material> material,
        const QString& shaderName
    );
private:
    QWidget* makeControl(const ReflectedUniform& uniform);
    QWidget* makeVector(const ReflectedUniform& uniform, int dimensions);
    QWidget* makeMatrix(const ReflectedUniform& uniform);
    std::shared_ptr<Material> material_;
    QVBoxLayout* root_ = nullptr;
};
