#include "TransformGizmo.h"
#include "../scene/SceneObject.h"
#include "../componentsBackend/TransformComponent/TransformComponent.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>
#include <cmath>
namespace
{
float distanceToLine(QPointF point,QPointF a,QPointF b)
{
    auto direction=b-a; double length=QPointF::dotProduct(direction,direction);
    double t=length>0?std::clamp(QPointF::dotProduct(point-a,direction)/length,0.0,1.0):0;
    return QLineF(point,a+direction*t).length();
}
QVector3D eulerAngles(const QMatrix4x4& m)
{
    // TransformComponent applies Rz * Ry * Rx.
    float y=std::asin(std::clamp(-m(2,0),-1.f,1.f));
    float x,z;
    if(std::abs(std::cos(y))>.0001f) {x=std::atan2(m(2,1),m(2,2));z=std::atan2(m(1,0),m(0,0));}
    else {x=0;z=std::atan2(-m(0,1),m(1,1));}
    return {qRadiansToDegrees(x),qRadiansToDegrees(y),qRadiansToDegrees(z)};
}
QMatrix4x4 rotationOnly(QMatrix4x4 m)
{
    // Remove scale. World rotation under a sheared parent is necessarily approximate.
    auto x=m.column(0).toVector3D().normalized();
    auto y=m.column(1).toVector3D(); y=(y-x*QVector3D::dotProduct(x,y)).normalized();
    auto z=QVector3D::crossProduct(x,y).normalized();
    QMatrix4x4 result; result.setColumn(0,QVector4D(x,0));result.setColumn(1,QVector4D(y,0));result.setColumn(2,QVector4D(z,0));
    return result;
}
}
bool TransformGizmo::project(const QVector3D& point,const QMatrix4x4& vp,const QSize& size,QPointF& screen)
{
    auto clip=vp*QVector4D(point,1);
    if(clip.w()<=.001f||clip.z() < -clip.w()||clip.z()>clip.w()) return false;
    screen=QPointF((clip.x()/clip.w()+1)*size.width()*.5,(1-clip.y()/clip.w())*size.height()*.5);
    return true;
}
bool TransformGizmo::prepare(const QMatrix4x4& vp,const QSize& size)
{
    if(!object) return false;
    transform_=object->getComponent<TransformComponent>(); if(!transform_) return false;
    auto model=transform_->getModelMatrix();origin_=model.map(QVector3D());
    if(!project(origin_,vp,size,center_)) return false;
    bool invertible=false; auto inverse=vp.inverted(&invertible); if(!invertible) return false;
    auto clip=vp*QVector4D(origin_,1); auto ndc=clip.toVector3DAffine();
    auto beside=inverse.map(QVector3D(ndc.x()+180.f/std::max(1,size.width()),ndc.y(),ndc.z()));
    length_=std::max(.001f,(beside-origin_).length());
    auto rotation=rotationOnly(model);
    for(int axis=0;axis<3;++axis)
    {
        QVector3D unit;unit[axis]=1;
        axes_[axis]=(local||mode==Scale)?rotation.mapVector(unit):unit;
        if(!project(origin_+axes_[axis]*length_,vp,size,ends_[axis])) ends_[axis]=center_;
        rings_[axis].clear();
        auto u=QVector3D::crossProduct(axes_[axis],std::abs(axes_[axis].y())<.9f?QVector3D(0,1,0):QVector3D(1,0,0)).normalized();
        auto v=QVector3D::crossProduct(axes_[axis],u);
        for(int i=0;i<=64;++i)
        {
            float angle=2*M_PI*i/64; QPointF p;
            if(project(origin_+(u*std::cos(angle)+v*std::sin(angle))*length_*.8f,vp,size,p)) rings_[axis].push_back(p);
        }
    }
    return true;
}
int TransformGizmo::hit(const QPointF& mouse) const
{
    if(mode==Scale&&QLineF(mouse,center_).length()<9) return 3;
    int found=-1; float closest=9;
    for(int axis=0;axis<3;++axis)
    {
        if(mode==Rotate)
        {
            const auto& ring=rings_[axis];
            for(size_t i=1;i<ring.size();++i)
            {float distance=distanceToLine(mouse,ring[i-1],ring[i]);if(distance<closest){closest=distance;found=axis;}}
        }
        else if(QLineF(center_,ends_[axis]).length()>15)
        {float distance=distanceToLine(mouse,center_+(ends_[axis]-center_)*.2,ends_[axis]);if(distance<closest){closest=distance;found=axis;}}
    }
    return found;
}
void TransformGizmo::paint(QPainter& p,const QMatrix4x4& vp,const QSize& size)
{
    if(!dragging()&&!prepare(vp,size)) return;
    if(!object) return;
    p.save();
    if(dragging()&&mode==Move)
    {QPointF current;if(project(transform_->getModelMatrix().map(QVector3D()),vp,size,current))p.translate(current-center_);}
    const QColor colors[]={QColor(245,80,80),QColor(95,225,115),QColor(85,155,255)};
    p.setRenderHint(QPainter::Antialiasing);
    for(int axis=0;axis<3;++axis)
    {
        p.setPen(QPen(active_==axis?QColor(255,225,80):colors[axis],3));
        if(mode==Rotate)
        {QPainterPath path;const auto& ring=rings_[axis];if(!ring.empty())path.moveTo(ring[0]);for(size_t i=1;i<ring.size();++i)path.lineTo(ring[i]);p.drawPath(path);}
        else
        {
            p.drawLine(center_,ends_[axis]);p.setBrush(colors[axis]);
            if(mode==Scale)p.drawRect(QRectF(ends_[axis]-QPointF(4,4),QSizeF(8,8)));
            else p.drawEllipse(ends_[axis],4,4);
            p.drawText(ends_[axis]+QPointF(6,-6),QString("XYZ")[axis]);
        }
    }
    p.setPen(Qt::white);p.setBrush(QColor(230,230,230));p.drawRect(QRectF(center_-QPointF(4,4),QSizeF(8,8)));
    p.restore();
}
bool TransformGizmo::begin(const QPointF& mouse,const QMatrix4x4& vp,const QSize& size)
{
    if(!prepare(vp,size)) return false;
    active_=hit(mouse);if(active_<0)return false;
    mouseStart_=mouse;position_=transform_->position;rotation_=transform_->rotation;scale_=transform_->scale;
    parent_.setToIdentity();if(auto* parent=object->parent())if(auto* t=parent->getComponent<TransformComponent>())parent_=t->getModelMatrix();
    bool invertible=false;parent_.inverted(&invertible);if(!invertible){active_=-1;return false;}
    initialRotation_.setToIdentity();initialRotation_.rotate(rotation_.z(),0,0,1);initialRotation_.rotate(rotation_.y(),0,1,0);initialRotation_.rotate(rotation_.x(),1,0,0);
    return true;
}
void TransformGizmo::drag(const QPointF& mouse)
{
    if(!dragging()||!transform_)return;
    auto delta=mouse-mouseStart_;
    if(mode==Rotate)
    {
        // Mouse angle is measured around the projected rotation ring's center.
        auto first=mouseStart_-center_,now=mouse-center_;
        float degrees=qRadiansToDegrees(std::atan2(now.y(),now.x())-std::atan2(first.y(),first.x()));
        degrees=std::remainder(degrees,360.f);if(snap)degrees=std::round(degrees/15)*15;
        // Match the axis orientation seen by the camera.
        auto u=rings_[active_];float area=0;
        for(size_t i=1;i<u.size();++i)area+=(u[i-1].x()-center_.x())*(u[i].y()-center_.y())-(u[i].x()-center_.x())*(u[i-1].y()-center_.y());
        if(area<0)degrees=-degrees;
        QMatrix4x4 turn;
        if(local){QVector3D axis;axis[active_]=1;turn.rotate(degrees,axis);turn=initialRotation_*turn;}
        else {turn.rotate(degrees,axes_[active_]);auto parentRotation=rotationOnly(parent_);turn=parentRotation.inverted()*turn*parentRotation*initialRotation_;}
        transform_->rotation=eulerAngles(turn);return;
    }
    float amount=0;
    if(active_==3)amount=(delta.x()-delta.y())/120.f;
    else
    {auto direction=ends_[active_]-center_;amount=QPointF::dotProduct(delta,direction)/std::max(1.0,QPointF::dotProduct(direction,direction));}
    if(mode==Move)
    {
        amount*=length_;if(snap)amount=std::round(amount/.5f)*.5f;
        transform_->position=position_+parent_.inverted().mapVector(axes_[active_]*amount);
    }
    else
    {
        if(snap)amount=std::round(amount/.1f)*.1f;
        transform_->scale=scale_;
        for(int axis=0;axis<3;++axis)if(active_==3||active_==axis)
        {float value=scale_[axis]*(1+amount);transform_->scale[axis]=std::copysign(std::max(.001f,std::abs(value)),scale_[axis]);}
    }
}
bool TransformGizmo::finish(bool cancel)
{
    if(!dragging())return false;
    bool changed=transform_->position!=position_||transform_->rotation!=rotation_||transform_->scale!=scale_;
    if(cancel){transform_->position=position_;transform_->rotation=rotation_;transform_->scale=scale_;}
    active_=-1;transform_=nullptr;return changed&&!cancel;
}
