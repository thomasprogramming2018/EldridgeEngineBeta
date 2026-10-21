#pragma once
#include <QDialog>
#include <atomic>
#include <thread>
#include "../renderer/SoftwareRayTracer.h"
#include "../renderer/VulkanRayTracer.h"
class QLabel;
class QProgressBar;
class QPushButton;
class QComboBox;

class RayTraceDialog : public QDialog
{
    Q_OBJECT
public:
    RayTraceDialog(const Scene& scene, const Camera& camera, QWidget* parent = nullptr);
    ~RayTraceDialog() override;
signals:
    void viewportRequested(int width, int samples);
private:
    void startRender();
    SoftwareRayTracer tracer_;
    Camera camera_;
    RaySceneSnapshot hardwareScene_;
    QComboBox* backend_;
    QComboBox* resolution_;
    QImage image_;
    std::thread worker_;
    std::atomic<bool> cancel_ {false};
    QLabel* preview_;
    QProgressBar* progress_;
    QPushButton* render_;
    QComboBox* samples_;
};
