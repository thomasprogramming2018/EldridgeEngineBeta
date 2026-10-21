#pragma once
#include <QWidget>
class LightComponent;
class LightWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LightWidget(LightComponent& light, QWidget* parent = nullptr);
signals:
    void lightChanged();
};
