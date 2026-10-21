#pragma once
#include "SoftwareRayTracer.h"
#include "../componentsBackend/LightComponent/LightComponent.h"
#include <QString>
#include <QByteArray>
#include <memory>

struct RaySceneSnapshot
{
    std::vector<RayTriangle> triangles;
    std::vector<RayMaterial> materials;
    std::vector<LightData> lights;
    bool sceneLights = false;
    static RaySceneSnapshot capture(const Scene& scene);
};
struct HardwareRaySettings
{
    int samples = 32;
    int bounces = 4;
    float exposure = 1.f;
    bool denoise = true;
    bool fullFrame = false;
    bool moving = false;
    bool globalIllumination = true;
    int denoisePasses = 3;
    int movingSamples = 2;
    float denoiseStrength = 1.f;
    float indirectClamp = 10.f;
    float sunIntensity = 3.f, skyIntensity = 1.f, sunAngle = 0.53f;
};
struct HardwareRaySupport
{
    bool available = false;
    QString device;
    QString reason;
};

// Linear HDR lighting and world normals/depth. Qt shares the byte arrays cheaply.
// Vulkan denoises these buffers. OpenGL reprojects history and upsamples for display.
struct RayLightingFrame
{
    int width = 0, height = 0;
    QByteArray lighting;
    QByteArray surface;
    Camera camera;
    bool fullFrame = false;
    bool moving = false;
    bool cumulative = false;
    bool gpuDenoised = false;
    int denoisePasses = 0;
    int historySamples = 1;
    int sceneUploads = 0;
    double renderMilliseconds = 0;
    double gpuMilliseconds = -1; // Unavailable on queues without timestamp support.
    double setupMilliseconds = 0;
};

// Vulkan owns its own device. Images are copied back for Qt/OpenGL presentation.
// A supported Vulkan ray-query device is required: no CPU fallback is disguised as GPU tracing.
class VulkanRayTracer
{
public:
    VulkanRayTracer();
    ~VulkanRayTracer();
    VulkanRayTracer(const VulkanRayTracer&) = delete;
    VulkanRayTracer& operator=(const VulkanRayTracer&) = delete;

    // Keep this object on one worker thread. Camera movement only changes uniforms.
    bool setScene(const RaySceneSnapshot& scene, QString& error);
    RayLightingFrame traceLighting(const Camera& camera, int width, int height,
                                  unsigned int seed, std::atomic<bool>& cancel, QString& error);
    RayLightingFrame traceFrame(const Camera& camera, int width, int height, unsigned int seed,
                               int samples, const HardwareRaySettings& settings,
                               std::atomic<bool>& cancel, QString& error, int sampleStart = 0);
    static HardwareRaySupport support();
    static QImage render(const RaySceneSnapshot& scene, const Camera& camera, int width, int height,
                         const HardwareRaySettings& settings, std::atomic<bool>& cancel,
                         const std::function<void(const QImage&, int)>& progress, QString& error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
