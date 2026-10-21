#include "modules/renderer/SoftwareRayTracer.h"
#include "modules/scene/Scene.h"
#include "modules/material/MaterialComponent.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    try
    {
        Scene scene;
        auto& cube=scene.createCube();
        auto& component=cube.addComponent<MaterialComponent>();
        component.material=std::make_shared<Material>();
        component.material->parameters["uBaseColor"]=QVector3D(1,0.02f,0.02f);
        SoftwareRayTracer tracer; tracer.capture(scene);
        require(tracer.triangleCount()==12,"Capture cube triangles");
        scene.removeObject(&cube); // Renderer uses a safe CPU snapshot.
        Camera camera; camera.position={0,0,3}; camera.yaw_=-90; camera.pitch_=0;
        std::atomic<bool> cancel{false}; int progress=0;
        auto image=tracer.render(camera,48,32,1,cancel,[&](int value){progress=value;});
        require(!image.isNull() && progress==100,"Complete ray tracing");
        auto center=image.pixelColor(24,16);
        require(center.red()>center.green()+30,"Trace and shade visible red geometry");
        cancel=true;
        require(tracer.render(camera,48,32,1,cancel,{}).isNull(),"Cancel rendering");
        tracer.capture(scene); require(tracer.triangleCount()==0,"Empty scene clears BVH");
        cancel=false;
        auto sky=tracer.render(camera,48,32,1,cancel,{});
        require(sky.pixelColor(24,16)!=center,"Missed rays show environment");
    }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
