#include "HardwareViewport.h"
#include <QElapsedTimer>
#include <algorithm>
#include <chrono>

HardwareViewport::HardwareViewport(QObject* parent) : QObject(parent), thread_([this]() { run(); }) {}
HardwareViewport::~HardwareViewport()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        pending_.reset();
        cancel_ = true;
        ++generation_;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}
void HardwareViewport::request(std::shared_ptr<const RaySceneSnapshot> scene, const Camera& camera,
                               int width, int height, const HardwareRaySettings& settings)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Camera requests replace pending work, but let the current small pass finish.
        // A new scene must reject all frames from the previous scene.
        if (requestedScene_ != scene)
        {
            requestedScene_ = scene;
            cancel_ = true;
            ++generation_;
        }
        pending_ = Job{std::move(scene), camera, width, height, settings, generation_.load()};
    }
    wake_.notify_one();
}
void HardwareViewport::cancel()
{
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_ = true;
    pending_.reset();
    requestedScene_.reset();
    ++generation_;
    wake_.notify_one();
}
void HardwareViewport::run()
{
    VulkanRayTracer tracer;
    std::shared_ptr<const RaySceneSnapshot> uploaded;
    std::optional<Job> current;
    unsigned int seed = 0;
    int samples = 0;
    for (;;)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&]()
            {
                return stopping_ || pending_ ||
                    (current && current->generation == generation_ && samples < current->settings.samples);
            });
            if (stopping_) return;
            if (pending_)
            {
                current = std::move(pending_);
                pending_.reset();
                samples = 0;
            }
            cancel_ = false;
        }
        Job& job = *current;
        QString error;
        if (uploaded != job.scene)
        {
            if (tracer.setScene(*job.scene, error)) uploaded = job.scene;
        }
        QElapsedTimer clock;
        clock.start();
        RayLightingFrame frame;
        int batch = job.settings.fullFrame ? (job.settings.moving ? std::clamp(job.settings.movingSamples,1,4) : 4) : 1;
        batch = std::min(batch, job.settings.samples - samples);
        if (error.isEmpty() && !cancel_)
        {
            bool accumulate = job.settings.fullFrame && !job.settings.moving;
            frame = tracer.traceFrame(job.camera, job.width, job.height, accumulate ? 0 : seed,
                                      batch, job.settings, cancel_, error, accumulate ? samples : 0);
        }
        seed += batch;
        if (frame.width && generation_ == job.generation && !cancel_)
        {
            samples += batch;
            frame.historySamples = samples;
            int percent = std::min(100, samples * 100 / std::max(1, job.settings.samples));
            QMetaObject::invokeMethod(this, [this, token = job.generation, frame, percent]()
            {
                if (generation_ == token) emit frameReady(frame, percent);
            }, Qt::QueuedConnection);
        }
        if (!error.isEmpty() && generation_ == job.generation)
        {
            samples = job.settings.samples;
            QMetaObject::invokeMethod(this, [this, error, token = job.generation]()
            {
                if (generation_ == token) emit failed(error);
            }, Qt::QueuedConnection);
        }
        // Lighting has its own cadence; never block the raster/UI thread.
        // New camera input wakes this wait immediately.
        int cadence = job.settings.fullFrame ? 16 : 33;
        int remaining = std::max(0, cadence - int(clock.elapsed()));
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(remaining), [&]() { return stopping_ || pending_.has_value(); });
    }
}
