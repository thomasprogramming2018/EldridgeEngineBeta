#include "RayTraceDialog.h"
#include <QComboBox>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

RayTraceDialog::RayTraceDialog(const Scene& scene, const Camera& camera, QWidget* parent)
    : QDialog(parent), camera_(camera)
{
    setWindowTitle("Ray-Traced Preview");
    resize(820, 560);
    tracer_.capture(scene);
    hardwareScene_=RaySceneSnapshot::capture(scene);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QString("%1 triangles — choose CPU or hardware GPU tracing below").arg(tracer_.triangleCount()),this));
    layout->addWidget(new QLabel("GPU traces GGX lighting, GI, shadows, reflections and normal maps. Custom GLSL is not evaluated. CPU supports fewer material features.",this));
    preview_ = new QLabel("Click Render to trace the current scene and camera.",this);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumSize(640,360);
    layout->addWidget(preview_);
    backend_=new QComboBox(this);
    backend_->addItem("CPU software ray tracing");
    auto support=VulkanRayTracer::support();
    if(support.available) {backend_->addItem("Vulkan GPU — "+support.device);backend_->setCurrentIndex(1);}
    else layout->addWidget(new QLabel("GPU unavailable: "+support.reason,this));
    layout->addWidget(backend_);
    resolution_=new QComboBox(this);resolution_->addItems({"640 × 360","960 × 540","1280 × 720"});layout->addWidget(resolution_);
    samples_ = new QComboBox(this);
    samples_->addItem("Fast — 1 sample per pixel",1);
    samples_->addItem("Preview — 4 samples per pixel",4);
    samples_->addItem("Fine — 16 samples per pixel",16);
    if(support.available) {samples_->addItem("GPU — 64 samples per pixel",64);samples_->addItem("GPU — 256 samples per pixel",256);}
    layout->addWidget(samples_);
    connect(backend_,&QComboBox::currentIndexChanged,this,[this](int index)
    {
        samples_->clear();samples_->addItem("Fast — 1 sample per pixel",1);
        samples_->addItem("Preview — 4 samples per pixel",4);samples_->addItem("Fine — 16 samples per pixel",16);
        if(index==1){samples_->addItem("GPU — 64 samples per pixel",64);samples_->addItem("GPU — 256 samples per pixel",256);}
    });
    progress_ = new QProgressBar(this);
    layout->addWidget(progress_);
    render_ = new QPushButton("Render",this);
    layout->addWidget(render_);
    connect(render_,&QPushButton::clicked,this,&RayTraceDialog::startRender);
    auto* cancel = new QPushButton("Cancel render",this);
    connect(cancel,&QPushButton::clicked,this,[this]() { cancel_ = true; });
    layout->addWidget(cancel);
    auto* save = new QPushButton("Save Image...",this);
    connect(save,&QPushButton::clicked,this,[this]()
    {
        if (image_.isNull()) return;
        QString path = QFileDialog::getSaveFileName(this,"Save Ray-Traced Image","Preview.png","PNG Image (*.png)");
        if (!path.isEmpty() && !image_.save(path,"PNG")) QMessageBox::warning(this,"Image","Cannot save image.");
    });
    layout->addWidget(save);
    auto* useViewport = new QPushButton("Use this result in the viewport", this);
    useViewport->setEnabled(backend_->currentIndex() == 1);
    useViewport->setToolTip("Use the GPU path-tracing shader live, with progressive refinement while stationary.");
    connect(backend_, &QComboBox::currentIndexChanged, useViewport, [useViewport](int index) { useViewport->setEnabled(index == 1); });
    connect(useViewport, &QPushButton::clicked, this, [this]()
    {
        cancel_ = true;
        int widths[] = {640, 960, 1280};
        emit viewportRequested(widths[resolution_->currentIndex()], samples_->currentData().toInt());
        accept();
    });
    layout->addWidget(useViewport);
}
RayTraceDialog::~RayTraceDialog()
{
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
}
void RayTraceDialog::startRender()
{
    if (worker_.joinable()) worker_.join();
    cancel_ = false;
    progress_->setValue(0);
    render_->setEnabled(false);
    int samples = samples_->currentData().toInt();
    bool gpu=backend_->currentIndex()==1;
    int width=resolution_->currentIndex()==0?640:resolution_->currentIndex()==1?960:1280;
    int height=width*9/16;
    worker_ = std::thread([this,samples,gpu,width,height]()
    {
        QString error;
        QImage result;
        if(gpu)
        {
            HardwareRaySettings settings;settings.samples=samples;
            result=VulkanRayTracer::render(hardwareScene_,camera_,width,height,settings,cancel_,[this](const QImage& image,int percent)
            {
                QMetaObject::invokeMethod(this,[this,image,percent]()
                {progress_->setValue(percent);preview_->setPixmap(QPixmap::fromImage(image).scaled(800,450,Qt::KeepAspectRatio));},Qt::QueuedConnection);
            },error);
        }
        else result=tracer_.render(camera_,width,height,std::min(samples,16),cancel_,[this](int percent)
        { QMetaObject::invokeMethod(this,[this,percent]() {progress_->setValue(percent);},Qt::QueuedConnection); });
        QMetaObject::invokeMethod(this,[this,result,error]()
        {
            if (!result.isNull()) { image_ = result; preview_->setPixmap(QPixmap::fromImage(image_).scaled(800,450,Qt::KeepAspectRatio)); }
            if(!error.isEmpty()) QMessageBox::warning(this,"Hardware Ray Tracing",error);
            render_->setEnabled(true);
        },Qt::QueuedConnection);
    });
}
