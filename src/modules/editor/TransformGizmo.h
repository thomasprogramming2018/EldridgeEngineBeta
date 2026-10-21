#pragma once
#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QVector3D>
#include <array>
#include <vector>
class QPainter;
class SceneObject;
class TransformComponent;

// Small editor helper. The component continues to store plain position/rotation/scale.
class TransformGizmo
{
public:
    enum Mode { Move, Rotate, Scale };
    Mode mode=Move;
    bool local=false, snap=false;
    SceneObject* object=nullptr;
    bool dragging() const { return active_>=0; }
    void paint(QPainter& painter,const QMatrix4x4& viewProjection,const QSize& size);
    bool begin(const QPointF& mouse,const QMatrix4x4& viewProjection,const QSize& size);
    void drag(const QPointF& mouse);
    bool finish(bool cancel=false);
    void clear() { object=nullptr; transform_=nullptr; active_=-1; }
    static bool project(const QVector3D& point,const QMatrix4x4& vp,const QSize& size,QPointF& screen);
private:
    bool prepare(const QMatrix4x4& vp,const QSize& size);
    int hit(const QPointF& mouse) const;
    TransformComponent* transform_=nullptr;
    QMatrix4x4 parent_, initialRotation_;
    QVector3D origin_,position_,rotation_,scale_;
    std::array<QVector3D,3> axes_;
    std::array<QPointF,3> ends_;
    std::array<std::vector<QPointF>,3> rings_;
    QPointF center_,mouseStart_;
    float length_=1;
    int active_=-1;
};
