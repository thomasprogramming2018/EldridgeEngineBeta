#pragma once

#include <QGroupBox>

class MeshComponent;

class MeshComponentWidget : public QGroupBox
{
public:
    explicit MeshComponentWidget(const MeshComponent& component, QWidget* parent = nullptr);
};
