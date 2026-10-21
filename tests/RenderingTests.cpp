#include "modules/renderer/Renderer.h"
#include "modules/scene/Scene.h"
#include "modules/editor/LightingDemo.h"
#include "modules/componentsBackend/TransformComponent/TransformComponent.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QOpenGLContext>
#include <cstring>
#include <iostream>
#include <stdexcept>

void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int differences(const QImage& a,const QImage& b)
{
    int count=0;
    for(int y=0;y<a.height();y+=2) for(int x=0;x<a.width();x+=2)
        if(a.pixel(x,y)!=b.pixel(x,y)) ++count;
    return count;
}
int main(int argc,char** argv)
{
    QSurfaceFormat format; format.setVersion(3,3); format.setProfile(QSurfaceFormat::CoreProfile); format.setSamples(0);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc,argv);
    try
    {
        Scene scene;
        Renderer viewport(scene);
        auto& object=scene.createCube();
        auto& material=object.addComponent<MaterialComponent>(); material.material=viewport.material();
        auto& floor=scene.createCube("Floor");
        floor.getComponent<TransformComponent>()->scale={4,0.1f,4};
        floor.getComponent<TransformComponent>()->position={0,-1.1f,0};
        floor.addComponent<MaterialComponent>().material=viewport.material();
        viewport.resize(420,300);
        viewport.renderSettings.ambientOcclusion=false;
        viewport.show();
        QElapsedTimer clock; clock.start();
        while(!viewport.material()->shader && clock.elapsed()<10000) { app.processEvents(); QThread::msleep(5); }
        require(bool(viewport.material()->shader),"Initialize viewport PBR");
        auto baseline=viewport.grabFramebuffer();
        require(!baseline.isNull(),"Render HDR framebuffer");
        viewport.setSelectedObject(&object);
        auto selectionImage=viewport.grabFramebuffer();
        require(differences(baseline,selectionImage)>30,"Draw selection bounds and transform handles over the viewport");
        selectionImage.save("/tmp/eldridge-gizmo-test.png");
        viewport.setSelectedObject(nullptr);
        viewport.renderSettings.ambientOcclusion=true;
        auto ao=viewport.grabFramebuffer();
        require(differences(baseline,ao)>5,"AO changes occluded scene pixels");
        viewport.renderSettings.ambientOcclusion=false;
        viewport.renderSettings.volumetricFog=true;
        auto fog=viewport.grabFramebuffer();
        require(differences(baseline,fog)>100,"Volumetric fog changes scene pixels");
        viewport.renderSettings.volumetricFog=false;
        viewport.renderSettings.screenSpaceReflections=true;
        auto ssr=viewport.grabFramebuffer();
        require(differences(baseline,ssr)>0,"SSR finds reflected geometry");
        viewport.renderSettings.screenSpaceReflections=false;
        viewport.camera.yaw_=36.87f; viewport.camera.pitch_=63.43f;
        auto sky=viewport.grabFramebuffer();
        viewport.renderSettings.godRays=true;
        auto rays=viewport.grabFramebuffer();
        require(differences(sky,rays)>10,"God rays scatter visible sunlight");
        viewport.renderSettings.godRays=false;
        viewport.camera.yaw_=-127; viewport.camera.pitch_=-20;
        viewport.loadMaterialShader(viewport.material(),QString(ENGINE_SHADER_DIRECTORY)+"/HDRP.elsha");
        require(viewport.material()->parameters.contains("material.clearCoat"),"HDRP exposes clear coat");
        require(!viewport.grabFramebuffer().isNull(),"Render HDRP material");
        viewport.renderSettings.shadows=false;
        auto unshadowed=viewport.grabFramebuffer();
        viewport.renderSettings.shadows=true;
        auto shadowed=viewport.grabFramebuffer();
        require(differences(unshadowed,shadowed)>1,"Shadow maps shade occluded surfaces");
        viewport.material()->set("material.emissive",QVector3D(8,2,0));
        viewport.renderSettings.bloom=false;
        auto bright=viewport.grabFramebuffer();
        viewport.renderSettings.bloom=true;
        auto glow=viewport.grabFramebuffer();
        require(differences(bright,glow)>10,"HDR bloom spreads bright emission");
        // Temporal reuse must average a stable surface and reject disocclusion.
        viewport.makeCurrent();
        QOpenGLFunctions_3_3_Core gl; gl.initializeOpenGLFunctions();
        HybridLighting history; QString historyError;
        require(history.initialize(gl, historyError), "Initialize temporal lighting shader");
        RayLightingFrame synthetic; synthetic.width = synthetic.height = 8;
        synthetic.camera.position = {0, 0, 3}; synthetic.camera.yaw_ = -90; synthetic.camera.pitch_ = 0;
        synthetic.lighting.resize(8 * 8 * 16); synthetic.surface.resize(8 * 8 * 16);
        auto makeFrame = [&](QVector3D color, float planeZ, int samples = 1)
        {
            auto inverse = (synthetic.camera.projectionMatrix(8, 8) * synthetic.camera.viewMatrix()).inverted();
            for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
            {
                auto far = inverse.map(QVector3D(2.f * (x + 0.5f) / 8 - 1, 1 - 2.f * (y + 0.5f) / 8, 1));
                auto direction = (far - synthetic.camera.position).normalized();
                float distance = (planeZ - 3.f) / direction.z();
                float pixel[] = {color.x() * samples, color.y() * samples, color.z() * samples, float(samples)};
                float surface[] = {0, 0, 1, distance};
                std::memcpy(synthetic.lighting.data() + (y * 8 + x) * 16, pixel, 16);
                std::memcpy(synthetic.surface.data() + (y * 8 + x) * 16, surface, 16);
            }
            require(history.update(gl, synthetic, 32), "Update temporal history");
            std::vector<float> pixels(8 * 8 * 4);
            gl.glBindTexture(GL_TEXTURE_2D, history.lighting());
            gl.glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
            int center = (4 * 8 + 4) * 4;
            return QVector4D(pixels[center], pixels[center+1], pixels[center+2], pixels[center+3]);
        };
        // The Qt HUD can leave premultiplied-alpha blending enabled.
        gl.glEnable(GL_BLEND); gl.glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        makeFrame({1, 0, 0}, 0);
        auto accumulated = makeFrame({0, 0, 1}, 0);
        require(std::abs(accumulated.x() - 0.5f) < 0.01f && accumulated.w() == 2, "Accumulate stable lighting history");
        for (int i = 0; i < 6; ++i) accumulated = makeFrame({0, 0, 1}, 0);
        require(std::abs(accumulated.x() - 0.125f) < 0.01f && accumulated.w() == 8, "Keep history buffers valid across repeated updates");
        auto disoccluded = makeFrame({0, 1, 0}, 1);
        require(disoccluded.y() > 0.99f && disoccluded.w() == 1, "Reject history after a depth change");
        history.reset();
        require(makeFrame({0, 0, 1}, 1).w() == 1, "Clear history after scene edits");
        synthetic.fullFrame = true;
        makeFrame({2, 0, 0}, 3);
        auto skyHistory = makeFrame({0, 0, 2}, 3);
        require(std::abs(skyHistory.x() - 1) < 0.01f && skyHistory.w() == 2, "Accumulate HDR sky radiance in full-frame mode");
        synthetic.cumulative = true;
        makeFrame({4, 0, 0}, 3, 4);
        auto completeSum = makeFrame({0, 0, 4}, 3, 8);
        require(completeSum.z() > 3.99f && completeSum.x() < 0.01f && completeSum.w() == 8, "Present cumulative GPU samples without counting history twice");
        history.release(gl); viewport.doneCurrent();

        auto support=VulkanRayTracer::support();
        if(support.available)
        {
            int progress=0;QString failure;
            QObject::connect(&viewport,&Renderer::hardwareFrameReady,[&](int value){progress=value;});
            QObject::connect(&viewport,&Renderer::hardwareRayTracingFailed,[&](const QString& error){failure=error;});
            viewport.renderSettings.rayFullFrame = false;
            viewport.renderSettings.hardwareRayTracing=true;viewport.renderSettings.raySamples=4;viewport.renderSettings.rayWidth=320;
            viewport.update();clock.restart();
            while(progress<100&&failure.isEmpty()&&clock.elapsed()<20000){app.processEvents();QThread::msleep(5);}
            if(!failure.isEmpty())throw std::runtime_error(failure.toStdString());
            require(progress==100,"Present a hardware ray-traced viewport frame");
            auto hybrid = viewport.grabFramebuffer();
            viewport.renderSettings.lightingDebug = 1;
            auto lighting = viewport.grabFramebuffer();
            require(differences(hybrid, lighting) > 100, "Inspect hybrid lighting separately from direct raster lighting");
            int litPixels = 0;
            for (int y = 90; y < lighting.height(); y += 2) for (int x = 0; x < lighting.width(); x += 2)
            {
                auto color = lighting.pixelColor(x, y);
                if (color.red() + color.green() + color.blue() > 30) ++litPixels;
            }
            require(litPixels > 50, "Composite actual indirect lighting beyond the HUD");
            viewport.renderSettings.lightingDebug = 3;
            require(differences(lighting, viewport.grabFramebuffer()) > 100, "Inspect temporal history coverage");
            viewport.renderSettings.lightingDebug = 0;
            progress=0;viewport.camera.position.setX(viewport.camera.position.x()+1);viewport.update();clock.restart();
            while(progress<100&&failure.isEmpty()&&clock.elapsed()<20000){app.processEvents();QThread::msleep(5);}
            require(progress==100,"Restart hardware tracing after camera movement");
            require(viewport.hardwareLightingFrame().sceneUploads == 1, "Reuse viewport acceleration structures after camera movement");
            // Camera input should not starve lighting by cancelling every in-flight pass.
            int framesWhileMoving = 0;
            auto movingConnection = QObject::connect(&viewport, &Renderer::hardwareFrameReady, &viewport, [&](int) { ++framesWhileMoving; });
            clock.restart();
            while (clock.elapsed() < 500)
            {
                viewport.camera.position.setX(viewport.camera.position.x() + 0.002f);
                viewport.update(); app.processEvents(); QThread::msleep(5);
            }
            QObject::disconnect(movingConnection);
            require(framesWhileMoving > 2, "Hybrid lighting continues during camera movement");
            progress = 0;
            viewport.renderSettings.rayFullFrame = true;
            viewport.renderSettings.raySamples = 16;
            viewport.update(); clock.restart();
            while ((progress < 100 || !viewport.hardwareLightingFrame().fullFrame || viewport.hardwareLightingFrame().moving) && failure.isEmpty() && clock.elapsed() < 20000)
            { app.processEvents(); QThread::msleep(5); }
            require(progress == 100 && viewport.hardwareLightingFrame().fullFrame && !viewport.hardwareLightingFrame().moving, "Refine full-scene reference lighting in the viewport");
            require(viewport.hardwareLightingFrame().sceneUploads == 1, "Switch shading mode without rebuilding geometry");
            viewport.setSelectedObject(&object);
            auto pathWithGizmo=viewport.grabFramebuffer();
            pathWithGizmo.save("/tmp/eldridge-path-gizmo-test.png");
            viewport.setSelectedObject(nullptr);
            require(differences(pathWithGizmo,viewport.grabFramebuffer())>30,"Draw gizmos over full-frame Vulkan lighting");
            viewport.renderSettings.lightingDebug = 1;
            auto pathRadiance = viewport.grabFramebuffer();
            require(differences(pathRadiance, lighting) > 100, "Full viewport contains direct and sky radiance rather than additive indirect lighting");
            viewport.renderSettings.lightingDebug = 0;
            viewport.renderSettings.hardwareRayTracing=false;
            Scene demo; LightingDemo::populate(demo);
            Scene restoredDemo; QString demoError;
            require(restoredDemo.fromJson(demo.toJson(), demoError), "Save and restore the procedural lighting playground");
            viewport.setScene(demo); LightingDemo::frame(viewport.camera);
            auto demoImage = viewport.grabFramebuffer();
            require(!demoImage.isNull(), "Render lighting playground geometry");
            // Restore before the local demo scene goes out of scope.
            viewport.setScene(scene);
        }
        viewport.close();
    }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
