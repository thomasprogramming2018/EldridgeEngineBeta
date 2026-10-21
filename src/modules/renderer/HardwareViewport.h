#pragma once
#include <QObject>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <memory>
#include <thread>
#include "VulkanRayTracer.h"

// One worker owns Vulkan. Camera input replaces pending work; scene edits cancel it.
class HardwareViewport : public QObject
{
    Q_OBJECT
public:
    explicit HardwareViewport(QObject* parent=nullptr);
    ~HardwareViewport() override;
    void request(std::shared_ptr<const RaySceneSnapshot> scene,const Camera& camera,int width,int height,const HardwareRaySettings& settings);
    void cancel();
signals:
    void frameReady(const RayLightingFrame& frame,int percent);
    void failed(const QString& error);
private:
    struct Job { std::shared_ptr<const RaySceneSnapshot> scene; Camera camera; int width,height; HardwareRaySettings settings; quint64 generation; };
    void run();
    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Job> pending_;
    std::shared_ptr<const RaySceneSnapshot> requestedScene_;
    bool stopping_=false;
    std::atomic<bool> cancel_{false};
    std::atomic<quint64> generation_{0};
    std::thread thread_;
};
