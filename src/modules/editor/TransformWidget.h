#pragma once

#include <QWidget>

class TransformComponent;
class QVector3D;

class TransformWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TransformWidget(TransformComponent& transform, QWidget* parent = nullptr);

signals:
    void transformChanged();

private:
    QWidget* makeVectorRow(QVector3D& value, double step);
};
