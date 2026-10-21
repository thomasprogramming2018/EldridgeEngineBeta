#include "modules/editor/TransformGizmo.h"
#include "modules/scene/Scene.h"
#include "modules/project/ProjectFile.h"
#include "modules/componentsBackend/TransformComponent/TransformComponent.h"
#include "modules/componentsBackend/LightComponent/LightComponent.h"
#include "modules/renderer/RenderSettings.h"
#include "modules/renderer/camera.h"
#include <QApplication>
#include <QtMath>
#include <cmath>
#include <QJsonArray>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    try
    {
        Scene scene;
        auto& parent=scene.createObject("Parent");auto* pt=parent.getComponent<TransformComponent>();
        pt->position={2,1,0};pt->rotation={0,25,30};pt->scale={2,3,2};
        auto& child=scene.createLight(2,"Spot");child.setParent(&parent);
        auto* transform=child.getComponent<TransformComponent>();transform->position={1,0,0};
        auto* light=child.getComponent<LightComponent>();light->color={.8f,.2f,.1f};light->radius=.35f;light->intensity=55;
        light->innerCone=15;light->outerCone=35;light->shadows=false;
        scene.recordEdit();
        auto world=light->worldLight();require((world.position-transform->getModelMatrix().map(QVector3D())).length()<.0001,"Parented light position");
        require(std::abs(world.direction.length()-1)<.0001,"Normalized light direction");
        Scene loaded;QString error;require(loaded.fromJson(scene.toJson(),error),"Restore light component");
        auto* restored=loaded.getObjects()[1]->getComponent<LightComponent>();
        require(restored&&restored->toJson()==light->toJson(),"All light properties survive scene serialization");
        light->enabled=false;scene.recordEdit();require(collectLights(scene).empty()&&hasSceneLights(scene),"Disabled light does not enable the fallback sun");
        require(scene.undo(),"Undo light property edit");require(collectLights(scene).size()==1,"Undo restores the light");require(scene.redo()&&collectLights(scene).empty(),"Redo light property edit");
        // Snapshots replace objects, so reacquire pointers after undo/redo.
        auto& selected=*scene.getObjects()[1];transform=selected.getComponent<TransformComponent>();
        auto before=scene.toJson();auto invalid=before;auto objects=invalid["objects"].toArray();auto object=objects[1].toObject();auto data=object["light"].toObject();data["range"]=-3;object["light"]=data;objects[1]=object;invalid["objects"]=objects;
        require(!scene.fromJson(invalid,error)&&scene.toJson()==before,"Invalid lights reject atomically");
        QTemporaryDir temp;QString prefab=temp.filePath("Light.elprefab");
        require(ProjectFile::savePrefab(prefab,scene,selected,error),"Save light prefab");
        Scene instance;require(ProjectFile::loadPrefab(prefab,instance,error),"Load light prefab");
        require(instance.getObjects()[0]->getComponent<LightComponent>()!=nullptr,"Light prefab retains its component");

        TransformGizmo gizmo;gizmo.object=&selected;
        auto origin=transform->getModelMatrix().map(QVector3D());
        Camera camera;camera.position=origin+QVector3D(4,3,6);auto direction=(origin-camera.position).normalized();
        camera.yaw_=qRadiansToDegrees(std::atan2(direction.z(),direction.x()));camera.pitch_=qRadiansToDegrees(std::asin(direction.y()));
        QSize size(800,600);auto vp=camera.projectionMatrix(800,600)*camera.viewMatrix();QPointF center,end;
        require(TransformGizmo::project(origin,vp,size,center),"Project gizmo origin");
        auto ndc=(vp*QVector4D(origin,1)).toVector3DAffine();auto beside=vp.inverted().map(QVector3D(ndc.x()+180.f/800,ndc.y(),ndc.z()));float length=(beside-origin).length();
        require(TransformGizmo::project(origin+QVector3D(length,0,0),vp,size,end),"Project X handle");
        QPointF start=center+(end-center)*.75;
        require(gizmo.begin(start,vp,size),"Pick move gizmo X axis");gizmo.drag(start+(end-center)*.5);require(gizmo.finish(),"Commit move drag");
        require((transform->getModelMatrix().map(QVector3D())-origin-QVector3D(length*.5f,0,0)).length()<.001,"World move under rotated and scaled parent");
        scene.recordEdit();require(scene.undo(),"Undo gizmo move");
        gizmo.object=scene.getObjects()[1].get();transform=gizmo.object->getComponent<TransformComponent>();
        gizmo.mode=TransformGizmo::Scale;
        require(gizmo.begin(center,vp,size),"Pick uniform scale handle");auto scale=transform->scale;gizmo.drag(center+QPointF(60,-60));
        require((transform->scale-scale*2).length()<.001,"Uniform scale drag");
        require(!gizmo.finish(true)&&transform->scale==scale,"Escape restores the original transform");
        SceneObject rotationObject("Rotation test");gizmo.object=&rotationObject;gizmo.mode=TransformGizmo::Rotate;
        camera.position={0,0,5};camera.yaw_=-90;camera.pitch_=0;
        vp=camera.projectionMatrix(800,600)*camera.viewMatrix();
        TransformGizmo::project({},vp,size,center);
        ndc=(vp*QVector4D(0,0,0,1)).toVector3DAffine();
        beside=vp.inverted().map(QVector3D(ndc.x()+180.f/800,ndc.y(),ndc.z()));length=beside.length();
        QPointF ringPoint;TransformGizmo::project(QVector3D(1,1,0).normalized()*length*.8f,vp,size,ringPoint);
        require(gizmo.begin(ringPoint,vp,size),"Pick Z rotation ring");
        auto offset=ringPoint-center;gizmo.drag(center+QPointF(-offset.y(),offset.x()));
        require(gizmo.finish(),"Commit rotation drag");
        auto rotated=rotationObject.getComponent<TransformComponent>()->getModelMatrix().mapVector({1,0,0});
        require((rotated-QVector3D(0,-1,0)).length()<.001,"Rotation follows the displayed ring orientation");
        RenderSettings settings;settings.rayGI=false;settings.rayDenoisePasses=4;settings.rayMovingSamples=3;settings.rayIndirectClamp=0;settings.raySkyIntensity=2;
        RenderSettings copied;copied.fromJson(settings.toJson());require(copied.toJson()==settings.toJson(),"Project graphics round trip includes new quality controls");
        std::cout<<"Lights, prefabs, undo, parented gizmos and graphics settings passed\n";
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
