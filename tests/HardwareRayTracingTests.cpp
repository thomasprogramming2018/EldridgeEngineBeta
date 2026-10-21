#include "modules/renderer/VulkanRayTracer.h"
#include "modules/scene/Scene.h"
#include "modules/material/MaterialComponent.h"
#include <QCoreApplication>
#include "modules/componentsBackend/TransformComponent/TransformComponent.h"
#include <QElapsedTimer>
#include <cmath>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    auto support=VulkanRayTracer::support();
    if(!support.available) { std::cout<<support.reason.toStdString()<<'\n'; return 77; }
    std::cout<<"Hardware ray-query device: "<<support.device.toStdString()<<'\n';
    try
    {
        Scene scene;
        auto& cube=scene.createCube();
        auto& material=cube.addComponent<MaterialComponent>(); material.material=std::make_shared<Material>();
        material.material->parameters["uBaseColor"]=QVector3D(1,0.01f,0.01f);
        auto snapshot=RaySceneSnapshot::capture(scene);
        if(snapshot.triangles.size()!=12) throw std::runtime_error("Capture hardware geometry");
        Camera camera; camera.position={0,0,3}; camera.yaw_=-90; camera.pitch_=0;
        HardwareRaySettings settings; settings.samples=8; settings.bounces=3;
        std::atomic<bool> cancel{false}; QString error; int progress=0;
        auto image=VulkanRayTracer::render(snapshot,camera,64,48,settings,cancel,[&](const QImage& image,int percent)
        { if(image.isNull()) throw std::runtime_error("Empty progressive hardware image"); progress=percent; },error);
        if(image.isNull()) throw std::runtime_error(error.toStdString());
        if(progress!=100 || image.pixelColor(32,24).red()<image.pixelColor(32,24).green()+20)
            throw std::runtime_error("Hardware rays failed to shade the red cube");
        image.save("/tmp/eldridge-hardware-test.png");

        // Moving the camera must reuse the scene rather than rebuilding Vulkan.
        VulkanRayTracer realtime;
        QElapsedTimer clock; clock.start();
        if (!realtime.setScene(snapshot, error)) throw std::runtime_error(error.toStdString());
        std::cout << "Persistent scene setup: " << clock.elapsed() << " ms\n";
        double milliseconds = 0, gpuMilliseconds = 0;
        for (int sample = 0; sample < 12; ++sample)
        {
            camera.position.setX(float(sample) * 0.01f);
            auto frame = realtime.traceLighting(camera, 320, 180, sample, cancel, error);
            if (!error.isEmpty()) throw std::runtime_error(error.toStdString());
            if (frame.width != 320 || frame.sceneUploads != 1 || frame.lighting.size() != 320 * 180 * 16)
                throw std::runtime_error("Camera movement rebuilt or lost the GPU scene");
            const float* values = reinterpret_cast<const float*>(frame.lighting.constData());
            for (int i = 0; i < 320 * 180 * 4; ++i)
                if (!std::isfinite(values[i])) throw std::runtime_error("Invalid hybrid radiance");
            if (sample > 0) { milliseconds += frame.renderMilliseconds; gpuMilliseconds += frame.gpuMilliseconds; }
        }
        std::cout << "320x180 hybrid lighting + transfer, warm average: " << milliseconds / 11 << " ms\n";
        std::cout << "Ray dispatch GPU timestamp average: " << gpuMilliseconds / 11 << " ms\n";

        // The live full-frame mode must evaluate exactly the reference shader.
        camera.position = {0, 0, 3};
        settings.fullFrame = true; settings.samples = 4; settings.denoise = false;
        auto live = realtime.traceFrame(camera, 64, 48, 0, 4, settings, cancel, error);
        if (!live.fullFrame || live.sceneUploads != 1) throw std::runtime_error("Reuse scene for live reference shading");
        // A skipped display update must still be present in the next GPU sum.
        live = realtime.traceFrame(camera, 64, 48, 0, 4, settings, cancel, error, 4);
        if (!live.cumulative || live.historySamples != 8) throw std::runtime_error("Retain all stationary samples on the GPU");
        settings.samples = 8;
        auto reference = VulkanRayTracer::render(snapshot, camera, 64, 48, settings, cancel, {}, error);
        if (reference.isNull()) throw std::runtime_error(error.toStdString());
        const float* radiance = reinterpret_cast<const float*>(live.lighting.constData());
        for (int y = 0; y < 48; y += 3) for (int x = 0; x < 64; x += 3)
        {
            int pixel = (y * 64 + x) * 4;
            auto expected = reference.pixelColor(x, y);
            for (int axis = 0; axis < 3; ++axis)
            {
                float value = std::max(0.f, radiance[pixel + axis] / radiance[pixel + 3]);
                value = std::clamp((value * (2.51f * value + 0.03f)) / (value * (2.43f * value + 0.59f) + 0.14f), 0.f, 1.f);
                int display = int(std::pow(value, 1.f / 2.2f) * 255 + 0.5f);
                int channels[] = {expected.red(), expected.green(), expected.blue()};
                if (std::abs(display - channels[axis]) > 1) throw std::runtime_error("Live viewport differs from the reference shading function");
            }
        }

        // An emissive wall must light another surface through secondary rays.
        Scene bounceScene;
        auto& receiver = bounceScene.createCube();
        auto& receiverMaterial = receiver.addComponent<MaterialComponent>();
        receiverMaterial.material = std::make_shared<Material>();
        receiverMaterial.material->parameters["uBaseColor"] = QVector3D(0.8f, 0.8f, 0.8f);
        auto baseline = RaySceneSnapshot::capture(bounceScene);
        auto bright = baseline;
        // A large triangle behind the camera: invisible directly, visible to the cube.
        RayMaterial emitter; emitter.emission = {8, 0, 0};
        bright.materials.push_back(emitter);
        RayTriangle panel;
        panel.a = {-20, -20, 4}; panel.b = {20, -20, 4}; panel.c = {0, 20, 4};
        panel.na = panel.nb = panel.nc = {0, 0, -1}; panel.material = 1;
        bright.triangles.push_back(panel);
        camera.position = {0, 0, 3};
        auto gatherRed = [&](const RaySceneSnapshot& data)
        {
            if (!realtime.setScene(data, error)) throw std::runtime_error(error.toStdString());
            auto frame = realtime.traceLighting(camera, 32, 24, 7, cancel, error);
            if (frame.width != 32) throw std::runtime_error(error.toStdString());
            const float* pixels = reinterpret_cast<const float*>(frame.lighting.constData());
            float red = 0;
            for (int i = 0; i < 32 * 24; ++i) red += pixels[i * 4];
            return red;
        };
        float darkRed = gatherRed(baseline), brightRed = gatherRed(bright);
        if (brightRed <= darkRed + 10) throw std::runtime_error("Off-screen emission did not bounce onto visible geometry");

        // Compare the actual Vulkan filter with a high-sample reference.
        Scene room;
        auto addBox=[&](const char* name,QVector3D position,QVector3D scale,QVector3D color)
        {
            auto& box=room.createCube(name);auto* t=box.getComponent<TransformComponent>();t->position=position;t->scale=scale;
            auto& m=box.addComponent<MaterialComponent>();m.material=std::make_shared<Material>();
            m.material->parameters["uBaseColor"]=color;m.material->parameters["uRoughness"]=.7f;
        };
        addBox("Floor",{0,-1,0},{8,.1f,8},{.7f,.7f,.7f});
        addBox("Cube",{0,0,0},{1,1,1},{.8f,.5f,.2f});
        addBox("Wall",{-2,1,0},{.1f,4,6},{.8f,.1f,.1f});
        auto& lamp=room.createLight(3,"Softbox");lamp.getComponent<TransformComponent>()->position={0,4,1};
        lamp.getComponent<TransformComponent>()->rotation={-90,0,0};
        auto* lampData=lamp.getComponent<LightComponent>();lampData->intensity=18;lampData->width=3;lampData->height=3;
        auto roomData=RaySceneSnapshot::capture(room);
        if(!roomData.sceneLights||roomData.lights.size()!=1)throw std::runtime_error("Capture scene lights");
        camera.position={0,2,5};camera.yaw_=-90;camera.pitch_=-20;
        settings.fullFrame=true;settings.denoise=false;settings.bounces=4;settings.indirectClamp=0;
        settings.skyIntensity=.15f;
        if(!realtime.setScene(roomData,error))throw std::runtime_error(error.toStdString());
        auto traceSamples=[&](int count)
        {
            RayLightingFrame result;
            for(int start=0;start<count;start+=4)result=realtime.traceFrame(camera,160,100,0,4,settings,cancel,error,start);
            if(result.width!=160)throw std::runtime_error(error.toStdString());return result;
        };
        auto raw=traceSamples(8);auto referenceFrame=traceSamples(256);
        settings.denoise=true;auto filtered=traceSamples(8);
        if(!filtered.gpuDenoised||filtered.denoisePasses!=3)throw std::runtime_error("Vulkan filtering metadata");
        auto display=[](float v){v=std::max(0.f,v);return std::pow(std::clamp((v*(2.51f*v+.03f))/(v*(2.43f*v+.59f)+.14f),0.f,1.f),1.f/2.2f);};
        const auto* r=reinterpret_cast<const float*>(raw.lighting.constData());
        const auto* f=reinterpret_cast<const float*>(filtered.lighting.constData());
        const auto* high=reinterpret_cast<const float*>(referenceFrame.lighting.constData());
        const auto* guide=reinterpret_cast<const float*>(raw.surface.constData());
        double rawError=0,filteredError=0;int compared=0;
        QImage rawImage(160,100,QImage::Format_RGB32),filteredImage=rawImage,referenceImage=rawImage;
        for(int i=0;i<160*100;++i)
        {
            int offset=i*4;int rgb[3],smooth[3],expected[3];
            for(int channel=0;channel<3;++channel)
            {
                float target=display(high[offset+channel]/high[offset+3]);
                float noisy=display(r[offset+channel]/r[offset+3]),clean=display(f[offset+channel]/f[offset+3]);
                rgb[channel]=int(noisy*255);smooth[channel]=int(clean*255);expected[channel]=int(target*255);
                if(guide[offset+3]>0){rawError+=std::abs(noisy-target);filteredError+=std::abs(clean-target);++compared;}
            }
            rawImage.setPixel(i%160,i/160,qRgb(rgb[0],rgb[1],rgb[2]));
            filteredImage.setPixel(i%160,i/160,qRgb(smooth[0],smooth[1],smooth[2]));
            referenceImage.setPixel(i%160,i/160,qRgb(expected[0],expected[1],expected[2]));
        }
        rawImage.save("/tmp/eldridge-noisy.png");filteredImage.save("/tmp/eldridge-filtered.png");referenceImage.save("/tmp/eldridge-noise-reference.png");
        std::cout<<"8 spp display MAE vs 256 spp: raw "<<rawError/compared*255<<", filtered "<<filteredError/compared*255<<"\n";
        if(filteredError>=rawError*.95)throw std::runtime_error("Vulkan denoiser did not reduce reference error");
        // Switching GI off keeps direct lights/reflections but removes diffuse bounce energy.
        settings.denoise=false;settings.globalIllumination=false;auto noGI=traceSamples(64);
        settings.globalIllumination=true;auto withGI=traceSamples(64);
        auto energy=[](const RayLightingFrame& frame){const float* p=reinterpret_cast<const float*>(frame.lighting.constData());double sum=0;for(int i=0;i<frame.width*frame.height;++i)sum+=(p[i*4]+p[i*4+1]+p[i*4+2])/p[i*4+3];return sum;};
        if(energy(withGI)<=energy(noGI)*1.01)throw std::runtime_error("Global illumination switch did not add bounced lighting");
        // Every source type contributes; disabled scene lights never restore the hidden sun.
        settings.globalIllumination=false;settings.skyIntensity=0;settings.bounces=1;
        auto dark=roomData;dark.lights.clear();
        if(!realtime.setScene(dark,error))throw std::runtime_error(error.toStdString());
        auto unlitEnergy=energy(traceSamples(8));
        for(int type=0;type<4;++type)
        {
            auto lit=roomData;auto& source=lit.lights[0];source.type=type;source.color={1,.1f,.05f};
            source.angle=2;source.radius=.2f;source.innerCone=25;source.outerCone=60;
            if(!realtime.setScene(lit,error))throw std::runtime_error(error.toStdString());
            if(energy(traceSamples(8))<=unlitEnergy+5)throw std::runtime_error("A scene light type failed to illuminate geometry");
        }
        auto tooMany=roomData;tooMany.lights.resize(65);
        if(realtime.setScene(tooMany,error)||error.isEmpty())throw std::runtime_error("Report light count limit explicitly");
        Scene empty; auto sky=VulkanRayTracer::render(RaySceneSnapshot::capture(empty),camera,32,24,settings,cancel,{},error);
        if(sky.isNull()) throw std::runtime_error("Empty-scene hardware trace: "+error.toStdString());
        cancel=true;
        if(!VulkanRayTracer::render(snapshot,camera,32,24,settings,cancel,{},error).isNull())
            throw std::runtime_error("Cancel hardware rendering");
    }
    catch(const std::exception& exception) { std::cerr<<exception.what()<<'\n'; return 1; }
}
