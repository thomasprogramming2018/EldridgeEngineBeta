#include "Renderer.h"
#include "../componentsBackend/TerrainComponent/TerrainComponent.h"
#include "../componentsBackend/TransformComponent/TransformComponent.h"
#include "../scene/Scene.h"
#include "../componentsBackend/MeshComponent/MeshComponent.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QOpenGLContext>
#include <QMatrix4x4>
#include <QTimerEvent>
#include <QDebug>
#include <QPainter>
#include <QtMath>
#include <algorithm>
#include <type_traits>

void Renderer::setScene(Scene& scene)
{
    disconnect(sceneChangedConnection_);
    disconnect(sceneResetConnection_);
    disconnect(historyConnection_);
    if(hardware_) hardware_->cancel();
    raySceneDirty_=true; shadowDirty_=true; latestRayFrame_={}; post_.resetLighting();
    brushTerrain_ = nullptr; brushStroke_ = false; brushHitValid_ = false;
    gizmo_.finish(true); gizmo_.clear();
    scene_ = &scene;
    sceneResetConnection_ = connect(scene_, &Scene::objectsAboutToReset, this, [this]()
    { gizmo_.finish(true); gizmo_.object=nullptr; brushTerrain_ = nullptr; brushStroke_ = false; brushHitValid_ = false; });
    historyConnection_=connect(scene_,&Scene::historyChanged,this,[this]() {raySceneDirty_=true;shadowDirty_=true;update();});
    sceneChangedConnection_ = connect(scene_, &Scene::sceneChanged, this, [this]() {
        bool found=false;for(const auto& object:scene_->getObjects())if(object.get()==gizmo_.object)found=true;
        if(!found) {gizmo_.clear();} // Never dereference a deleted selection.
        sceneDirty_ = true;
        raySceneDirty_=true;
        update();
    });
    sceneDirty_ = true;
    keys_.clear();
    looking_ = false;
    unsetCursor();
    update();
}


Renderer::Renderer(Scene& scene, QWidget* parent)
    : QOpenGLWidget(parent), material_(std::make_shared<Material>())
{
    hardware_=std::make_unique<HardwareViewport>();
    connect(hardware_.get(), &HardwareViewport::frameReady, this, [this](const RayLightingFrame& frame, int percent)
    {
        if (!renderSettings.hardwareRayTracing || raySceneDirty_ || frame.fullFrame != renderSettings.rayFullFrame) return;
        latestRayFrame_ = frame;
        rayProgress_ = percent;
        post_.setLightingFrame(frame);
        // Adapt after several frames, so one allocation or scheduling spike cannot
        // keep shrinking the buffer. This budget includes GPU wait and transfer.
        float budget = renderSettings.rayBudgetMs;
        slowRayFrames_ = frame.renderMilliseconds > budget * 1.3 ? slowRayFrames_ + 1 : 0;
        fastRayFrames_ = !frame.moving && frame.renderMilliseconds < budget * 0.6 ? fastRayFrames_ + 1 : 0;
        if (slowRayFrames_ >= 3 && frame.width > 160)
        {
            adaptiveRayWidth_ = std::max(160, int(frame.width * 0.8f) / 16 * 16);
            slowRayFrames_ = fastRayFrames_ = 0;
        }
        else if (fastRayFrames_ >= 12 && frame.width < renderSettings.rayWidth)
        {
            adaptiveRayWidth_ = std::min(renderSettings.rayWidth, frame.width + 32);
            slowRayFrames_ = fastRayFrames_ = 0;
        }
        emit hardwareFrameReady(percent);
        update();
    });
    connect(hardware_.get(),&HardwareViewport::failed,this,[this](const QString& error)
    { renderSettings.hardwareRayTracing=false;latestRayFrame_={};post_.resetLighting();emit statusText(error);emit hardwareRayTracingFailed(error);update(); });
    setScene(scene);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(420, 300);
    timerId_ = startTimer(16);
    frameClock_.start();
    timeClock_.start();
}
Renderer::~Renderer()
{
    hardware_.reset();
    if (timerId_) killTimer(timerId_);
    if (glInitialized_ && context()) releaseGL();
}
void Renderer::releaseGL()
{
    if (!glInitialized_ || !context()) return;
    makeCurrent();
    for (auto& entry : meshes_) entry.second->release(*this);
    meshes_.clear();
    terrains_.clear();
    post_.release(*this);
    shadows_.release(*this);
    shadowsReady_=false;
    postReady_ = false;
    material_->shader.reset();
    resources_.releaseGL();
    ready_ = false;
    glInitialized_ = false;
    doneCurrent();
}
void Renderer::initializeGL()
{
    initializeOpenGLFunctions();
    glInitialized_ = true;
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
            [this](){ releaseGL(); }, Qt::DirectConnection);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.12f, 0.14f, 0.18f, 1.f);

    sceneDirty_ = true;
    QString error;
    shadowsReady_=shadows_.initialize(*this,error);
    if(!shadowsReady_) emit statusText(error);
    postReady_ = post_.initialize(*this, error);
    if (!postReady_) emit statusText("Post-processing failed: " + error);
    auto shader = resources_.loadShaderAsset(
        QString(ENGINE_SHADER_DIRECTORY) + "/PBR.elsha", error);
    material_->setShader(shader);
    if (!error.isEmpty())
        emit statusText(error);
    if (material_->shader) {
        if (material_->parameters.contains("uRoughness")) material_->set("uRoughness", 0.65f);
        if (material_->parameters.contains("uBands")) material_->set("uBands", 1);
        emit statusText("Shader reflection ready • GPU resources initialized");
    } else {
        emit statusText("Shader compilation failed • check application output");
    }

    emit shaderChanged();
}
void Renderer::resizeGL(int w, int h) { glViewport(0,0,w,h); }
void Renderer::selectShader(const QString& key)
{
    shaderKey_ = key;
    loadMaterialShader(material_, QString(ENGINE_SHADER_DIRECTORY) + "/" + key + ".elsha");
}
void Renderer::focusCube()
{
    camera.nearPlane=0.05f;camera.farPlane=500.f;
    camera.position = {3.0f, 2.0f, 4.0f};
    camera.yaw_ = -127.f;
    camera.pitch_ = -20.f;
    update();
    setFocus();
}
void Renderer::resetCamera() { focusCube(); }


void Renderer::changeMaterialShader(
    std::shared_ptr<Material> material,
    const QString& shaderName)
{
    loadMaterialShader(material, QString(ENGINE_SHADER_DIRECTORY) + "/" + shaderName + ".elsha");
}

void Renderer::paintGL()
{
    // QPainter draws the HUD after our passes. Restore the state those passes own.
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (!fpsClock_.isValid()) fpsClock_.start();
    ++fpsFrames_;
    if (fpsClock_.elapsed() >= 500)
    {
        rasterFps_ = fpsFrames_ * 1000.f / fpsClock_.elapsed();
        fpsFrames_ = 0;
        fpsClock_.restart();
    }
    const float time = float(timeClock_.elapsed() / 1000.0);
    int pixelWidth = std::max(1, int(width() * devicePixelRatioF()));
    int pixelHeight = std::max(1, int(height() * devicePixelRatioF()));
    bool postActive = postReady_ && post_.begin(*this, pixelWidth, pixelHeight);
    if (postActive)
    {
        const GLfloat background[] = {0.035f, 0.055f, 0.09f, 1.f};
        glClearBufferfv(GL_COLOR, 0, background);
        glClear(GL_DEPTH_BUFFER_BIT);
    }
    else
    {
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        glViewport(0, 0, pixelWidth, pixelHeight);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    for (const auto& object : scene_->getObjects())
        if (auto* terrain = object->getComponent<TerrainComponent>())
            if (!terrains_.count(terrain) || terrains_[terrain].revision != terrain->revision) sceneDirty_ = true;
    if (sceneDirty_)
    {
        raySceneDirty_=true; shadowDirty_=true;
        // Restored scenes contain CPU material values and shader paths.
        // Compile their GPU programs here, while OpenGL is current.
        QHash<QString, std::shared_ptr<Shader>> restoredShaders;
        for (const auto& object : scene_->getObjects())
        {
            auto* component = object->getComponent<MaterialComponent>();
            if (!component || !component->material) continue;
            auto& material = *component->material;
            if (material.shader && material.shader->programId())
                restoredShaders.insert(material.shaderPath, material.shader);
            else if (!material.shaderPath.isEmpty())
            {
                QString error;
                auto shader = restoredShaders.value(material.shaderPath);
                if (!shader) shader = resources_.loadShaderAsset(material.shaderPath, error);
                if (shader)
                {
                    material.setShader(shader);
                    restoredShaders.insert(material.shaderPath, shader);
                }
                else emit statusText(error);
            }
        }
        for (auto& entry : meshes_) entry.second->release(*this);
        meshes_.clear();
        terrains_.clear();
        ready_ = false;
        for (const auto& object : scene_->getObjects())
        {
            auto* component = object->getComponent<MeshComponent>();
            if (!component || !component->mesh) continue;
            if (auto* terrain = object->getComponent<TerrainComponent>())
            {
                TerrainGpu data; data.revision = terrain->revision;
                data.weights = std::make_unique<QOpenGLTexture>(terrain->weights);
                data.weights->setWrapMode(QOpenGLTexture::ClampToEdge);
                data.weights->setMinMagFilters(QOpenGLTexture::Linear,QOpenGLTexture::Linear);
                terrains_[terrain] = std::move(data);
                for (const auto& chunk : terrain->chunks) for (const auto& level : chunk.levels)
                {
                    auto uploaded = std::make_unique<GPUMesh>();
                    if (uploaded->uploadToGPU(*this,*level)) { meshes_[level.get()] = std::move(uploaded); ready_ = true; }
                }
                continue;
            }
            auto mesh = std::make_unique<GPUMesh>();
            if (mesh->uploadToGPU(*this, *component->mesh))
            {
                meshes_[component->mesh.get()] = std::move(mesh);
                ready_ = true;
            }
        }
        sceneDirty_ = false;
        if (!brushStroke_) emit sceneResourcesReady();
    }
    updateHardwareLighting();
    if (!ready_)
    {
        if (postActive) post_.finish(*this, defaultFramebufferObject(), camera, renderSettings);
        QPainter painter(this);
    drawRenderStats(painter);
    drawEditorTools(painter);
        return;
    }

    if(shadowsReady_ && renderSettings.shadows && shadowDirty_)
    {
        std::vector<ShadowDraw> draws;
        QVector3D minimum(1e30f,1e30f,1e30f),maximum(-1e30f,-1e30f,-1e30f);
        for(const auto& object:scene_->getObjects())
        {
            auto* mesh=object->getComponent<MeshComponent>();if(!mesh||!mesh->mesh) continue;
            auto* transform=object->getComponent<TransformComponent>();auto model=transform?transform->getModelMatrix():QMatrix4x4();
            const auto& vertices=mesh->mesh->vertices;
            for(size_t i=0;i+7<vertices.size();i+=8)
            {
                auto position=model.map(QVector3D(vertices[i],vertices[i+1],vertices[i+2]));
                for(int axis=0;axis<3;++axis) {minimum[axis]=std::min(minimum[axis],position[axis]);maximum[axis]=std::max(maximum[axis],position[axis]);}
            }
            if(auto* terrain=object->getComponent<TerrainComponent>())
            {
                for(const auto& chunk:terrain->chunks)
                {auto uploaded=meshes_.find(chunk.levels[0].get());if(uploaded!=meshes_.end())draws.push_back({uploaded->second.get(),model});}
            }
            else {auto uploaded=meshes_.find(mesh->mesh.get());if(uploaded!=meshes_.end())draws.push_back({uploaded->second.get(),model});}
        }
        if(!draws.empty()) shadows_.render(*this,draws,minimum,maximum);
        shadowDirty_=false;
    }
    const auto sceneLights=collectLights(*scene_);
    bool explicitLights=hasSceneLights(*scene_);
    const QMatrix4x4 view = camera.viewMatrix();
    const QMatrix4x4 projection = camera.projectionMatrix(width(), height());
    auto putMatrix = [this](const Shader& shader, const QString& name, const QMatrix4x4& matrix) {
        const auto* u = shader.findUniform(name);
        if (u && u->location >= 0 && u->type == GL_FLOAT_MAT4)
            glUniformMatrix4fv(u->location, 1, GL_FALSE, matrix.constData());
    };

    for (const auto& object : scene_->getObjects())
    {
        auto* mesh = object->getComponent<MeshComponent>();
        auto* materialComponent =
            object->getComponent<MaterialComponent>();

        if (!mesh || !mesh->mesh)
            continue;

        if (!materialComponent || !materialComponent->material)
            continue;

        Material& material = *materialComponent->material;

        if (!material.shader || !material.shader->bind())
            continue;

        Shader& shader = *material.shader;
        if (postActive)
        {
            GLenum buffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
            bool surface = glGetFragDataLocation(shader.programId(), "Surface") >= 0;
            glDrawBuffers(surface ? 2 : 1, buffers);
        }

        const auto* transform = object->getComponent<TransformComponent>();
        putMatrix(shader, "uModel", transform ? transform->getModelMatrix() : QMatrix4x4());
        putMatrix(shader, "uView", view);
        putMatrix(shader, "uProjection", projection);
        if (const auto* u = shader.findUniform("uLightDirection"))
            if (u->location>=0 && u->type == GL_FLOAT_VEC3)
                glUniform3f(u->location, -0.4f, -1.0f, -0.3f);
        if (const auto* u = shader.findUniform("uCameraPosition"))
            if (u->location>=0 && u->type == GL_FLOAT_VEC3)
                glUniform3f(u->location, camera.position.x(), camera.position.y(), camera.position.z());
        if (const auto* u = shader.findUniform("uTime"))
        {
            if (u->location >= 0 && u->type == GL_FLOAT)
                glUniform1f(u->location, time);
        }
        // The shader uses this switch to choose its lit or unlit calculation.
        glUniform1i(shader.uniformLocation("uAffectedByLights"), shader.affectedByLights());
        glUniform3f(shader.uniformLocation("camera.Pos"), camera.position.x(), camera.position.y(), camera.position.z());
        glUniform1i(shader.uniformLocation("uSceneLights"),explicitLights);
        glUniform1i(shader.uniformLocation("lightCount"),std::min(32,int(sceneLights.size())));
        for(int i=0;i<std::min(32,int(sceneLights.size()));++i)
        {
            const auto& light=sceneLights[i];QString prefix="lights["+QString::number(i)+"].";
            auto put=[&](const QString& name,QVector3D v){glUniform3f(shader.uniformLocation(prefix+name),v.x(),v.y(),v.z());};
            put("position",light.position);put("direction",light.direction);put("ambient",QVector3D());
            put("diffuse",light.color*light.intensity);put("specular",light.color*light.intensity);
            glUniform1i(shader.uniformLocation(prefix+"type"),light.type);
            glUniform1f(shader.uniformLocation(prefix+"range"),light.range);
            glUniform4f(shader.uniformLocation(prefix+"shape"),std::cos(qDegreesToRadians(light.innerCone)),std::cos(qDegreesToRadians(light.outerCone)),light.width,light.height);
        }
        glUniform3f(shader.uniformLocation("dlight.dir"), -.4f, -1.f, -.3f);
        float brightness=shader.affectedByLights()&&!explicitLights?1.f:0.f;
        glUniform3f(shader.uniformLocation("dlight.ambient"),.2f*brightness,.2f*brightness,.2f*brightness);
        glUniform3f(shader.uniformLocation("dlight.diffuse"),brightness,brightness,brightness);
        glUniform3f(shader.uniformLocation("dlight.specular"),brightness,brightness,brightness);
        uploadMaterialUniforms(shader, material);
        glUniform1i(shader.uniformLocation("uShadows"),shadowsReady_&&renderSettings.shadows&&!explicitLights);
        if(shadowsReady_ && shader.uniformLocation("uShadowMap") >= 0)
        {
            shadows_.bind(*this,15);glUniform1i(shader.uniformLocation("uShadowMap"),15);
            glUniformMatrix4fv(shader.uniformLocation("uLightViewProjection"),1,GL_FALSE,shadows_.matrix().constData());
        }

        auto* terrain = object->getComponent<TerrainComponent>();
        glUniform1i(shader.uniformLocation("uTerrainEnabled"),terrain != nullptr);
        if (terrain)
        {
            auto gpu = terrains_.find(terrain);
            if (gpu == terrains_.end()) continue;
            gpu->second.weights->bind(8); glUniform1i(shader.uniformLocation("uTerrainWeights"),8);
            glUniform1f(shader.uniformLocation("uTerrainTiling"),terrain->tiling);
            for (int layer = 0; layer < 4; ++layer)
            {
                resources_.texture(terrain->layers[layer].texture)->bind(9+layer);
                glUniform1i(shader.uniformLocation("uTerrainLayer"+QString::number(layer)),9+layer);
                auto color=terrain->layers[layer].color;
                glUniform3f(shader.uniformLocation("uTerrainColor"+QString::number(layer)),color.x(),color.y(),color.z());
            }
            glUniform4f(shader.uniformLocation("uTerrainBrush"),brushHit_.x(),brushHit_.y(),brushHit_.z(),
                        brushTerrain_==terrain&&brushHitValid_?brushRadius_:0.f);
            auto model=transform?transform->getModelMatrix():QMatrix4x4();
            float chunkSize=terrain->size/(terrain->resolution-1)*32.f;
            for (const auto& chunk : terrain->chunks)
            {
                auto center=model.map(chunk.center);
                float scale=std::sqrt(model.column(0).toVector3D().lengthSquared()+model.column(1).toVector3D().lengthSquared()+model.column(2).toVector3D().lengthSquared());
                float radius=chunk.radius*scale;
                auto clip=projection*view;
                bool visible=true;
                for(int axis=0;axis<3;++axis) for(float sign:{-1.f,1.f})
                {
                    QVector4D plane=clip.row(3)+clip.row(axis)*sign;
                    if(QVector3D::dotProduct(plane.toVector3D(),center)+plane.w() < -radius*plane.toVector3D().length()) visible=false;
                }
                if(!visible) continue;
                float distance=(center-camera.position).length();
                int level=distance<chunkSize*2?0:distance<chunkSize*4?1:distance<chunkSize*8?2:3;
                auto uploaded=meshes_.find(chunk.levels[level].get());
                if(uploaded!=meshes_.end()) uploaded->second->draw(*this);
            }
        }
        else
        {
            auto uploaded = meshes_.find(mesh->mesh.get());
            if (uploaded != meshes_.end()) uploaded->second->draw(*this);
        }
    }
    if (postActive) post_.finish(*this, defaultFramebufferObject(), camera, renderSettings);
    QPainter painter(this);
    drawRenderStats(painter);
    drawEditorTools(painter);
}
void Renderer::uploadMaterialUniforms(const Shader& shader, const Material& material)
{
    GLint maxUnits = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxUnits);
    // Leave engine texture slots free when the shader uses those interfaces.
    if(shader.findUniform("uShadowMap")) maxUnits=std::min(maxUnits,15);
    if(shader.findUniform("uTerrainWeights")) maxUnits=std::min(maxUnits,8);
    int textureUnit = 0;
    for (const auto& u : shader.uniforms()) {
        if (!Material::isEditableUniform(u)) continue;
        const MaterialValue* stored = material.get(u.name);
        if (!stored) continue;
        std::visit([this, &u, &textureUnit, maxUnits](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T,float>) {
                if (u.type == GL_FLOAT) glUniform1f(u.location,value);
            } else if constexpr (std::is_same_v<T,int>) {
                if (u.type == GL_INT) glUniform1i(u.location,value);
            } else if constexpr (std::is_same_v<T,bool>) {
                if (u.type == GL_BOOL) glUniform1i(u.location,value?1:0);
            } else if constexpr (std::is_same_v<T,QVector2D>) {
                if (u.type == GL_FLOAT_VEC2) glUniform2f(u.location,value.x(),value.y());
            } else if constexpr (std::is_same_v<T,QVector3D>) {
                if (u.type == GL_FLOAT_VEC3) glUniform3f(u.location,value.x(),value.y(),value.z());
            } else if constexpr (std::is_same_v<T,QVector4D>) {
                if (u.type == GL_FLOAT_VEC4) glUniform4f(u.location,value.x(),value.y(),value.z(),value.w());
            } else if constexpr (std::is_same_v<T,QMatrix4x4>) {
                if (u.type == GL_FLOAT_MAT4) glUniformMatrix4fv(u.location,1,GL_FALSE,value.constData());
            } else if constexpr (std::is_same_v<T,TexturePath>) {
                if (u.type == GL_SAMPLER_2D && textureUnit < maxUnits) {
                    if (auto* texture = resources_.texture(value.path)) {
                        texture->bind(static_cast<GLuint>(textureUnit));
                        glUniform1i(u.location,textureUnit);
                        ++textureUnit;
                    }
                }
            }
        },*stored);
    }
}
void Renderer::moveCamera(float dt)
{
    const QVector3D forward = camera.forward();
    const QVector3D right = QVector3D::crossProduct(forward,QVector3D(0,1,0)).normalized();
    const float delta = speed_ * dt * (keys_.contains(Qt::Key_Shift) ? 3.f : 1.f);
    if (keys_.contains(Qt::Key_W)) camera.position += forward*delta;
    if (keys_.contains(Qt::Key_S)) camera.position -= forward*delta;
    if (keys_.contains(Qt::Key_D)) camera.position += right*delta;
    if (keys_.contains(Qt::Key_A)) camera.position -= right*delta;
    if (keys_.contains(Qt::Key_E)) camera.position.setY(camera.position.y()+delta);
    if (keys_.contains(Qt::Key_Q)) camera.position.setY(camera.position.y()-delta);
}
void Renderer::timerEvent(QTimerEvent* event)
{
    if (event->timerId()!=timerId_) return;
    const float dt = std::clamp(frameClock_.restart()/1000.f,0.f,0.1f);
    if (looking_ && !keys_.isEmpty()) moveCamera(dt);
    update();
}
void Renderer::keyPressEvent(QKeyEvent* e)
{
    if(e->key()==Qt::Key_Escape&&gizmo_.dragging())
    {gizmo_.finish(true);invalidateLighting();emit transformEdited();return;}
    if(!looking_&&(e->key()==Qt::Key_W||e->key()==Qt::Key_E||e->key()==Qt::Key_R))
    {int mode=e->key()==Qt::Key_W?0:e->key()==Qt::Key_E?1:2;setGizmoMode(mode);emit gizmoModeChanged(mode);return;}
    if (!e->isAutoRepeat()) keys_.insert(e->key());
    if (e->key()==Qt::Key_F) {if(gizmo_.object)focusObject(gizmo_.object);else focusCube();}
    QOpenGLWidget::keyPressEvent(e);
}
void Renderer::keyReleaseEvent(QKeyEvent* e)
{
    if (!e->isAutoRepeat()) keys_.remove(e->key());
    QOpenGLWidget::keyReleaseEvent(e);
}
void Renderer::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    if (e->button()==Qt::LeftButton && brushTerrain_)
    { brushStroke_=true; updateTerrainBrush(e->pos(),true); return; }
    if(e->button()==Qt::LeftButton)
    {
        auto vp=camera.projectionMatrix(width(),height())*camera.viewMatrix();
        if(gizmo_.begin(e->position(),vp,size()))return;
        emit objectPicked(pickObject(e->pos()));return;
    }
    if (e->button()==Qt::RightButton) {
        looking_=true;
        lastMouse_ = e->pos();
        setCursor(Qt::BlankCursor);
    }
}
void Renderer::mouseReleaseEvent(QMouseEvent* e)
{
    if(e->button()==Qt::LeftButton&&gizmo_.dragging())
    {if(gizmo_.finish())scene_->recordEdit();invalidateLighting();emit transformEdited();return;}
    if(e->button()==Qt::LeftButton && brushStroke_)
    { brushStroke_=false; scene_->recordEdit(); return; }
    if (e->button()==Qt::RightButton) {
        looking_=false;
        unsetCursor();
    }
}
void Renderer::mouseMoveEvent(QMouseEvent* e)
{
    if(gizmo_.dragging()) {gizmo_.drag(e->position());invalidateLighting();return;}
    if(brushTerrain_ && !looking_)
    { updateTerrainBrush(e->pos(),brushStroke_ && (e->buttons()&Qt::LeftButton)); return; }
    if (!looking_) return;
    const QPoint delta = e->pos()-lastMouse_;
    lastMouse_=e->pos();
    camera.yaw_ += delta.x()*0.20f;
    camera.pitch_ = std::clamp(camera.pitch_-delta.y()*0.20f,-89.f,89.f);
    update();
}
void Renderer::wheelEvent(QWheelEvent* e)
{
    speed_ = std::clamp(speed_ + e->angleDelta().y()/120.f*0.5f,0.5f,20.f);
    emit statusText(QString("Camera speed: %1").arg(speed_,0,'f',1));
}
void Renderer::focusOutEvent(QFocusEvent* e)
{
    if(gizmo_.dragging()){if(gizmo_.finish())scene_->recordEdit();invalidateLighting();emit transformEdited();}
    keys_.clear();
    looking_=false;
    if(brushStroke_) {brushStroke_=false;scene_->recordEdit();}
    unsetCursor();
    QOpenGLWidget::focusOutEvent(e);
}

void Renderer::loadMaterialShader(std::shared_ptr<Material> material, const QString& path)
{
    if (!material || !glInitialized_)
        return;

    makeCurrent();
    QString error;
    auto shader = resources_.loadShaderAsset(path, error);
    if (shader)
    {
        auto previousValues = material->parameters;
        material->setShader(shader);
        // These are material defaults, not engine lighting controls.
        if (material->parameters.contains("material.metallic") && !previousValues.contains("material.metallic")) material->set("material.metallic", 0.f);
        if (material->parameters.contains("material.emissive") && !previousValues.contains("material.emissive")) material->set("material.emissive", QVector3D(0, 0, 0));
        if (material->parameters.contains("material.normal_vector") && !previousValues.contains("material.normal_vector")) material->set("material.normal_vector", QVector3D(0, 0, 1));
        for (const QString& name : material->parameters.keys())
            if (name.startsWith("material.has") && !previousValues.contains(name))
                material->set(name, 0);
    }
    doneCurrent();

    if (!shader)
        emit statusText(error);
    else
        emit statusText("Loaded shader: " + path);
    emit shaderChanged();
    update();
}

void Renderer::setTerrainBrush(TerrainComponent* terrain,int mode,float radius,float strength,int layer,float flatHeight)
{
    if(brushStroke_) scene_->recordEdit();
    brushTerrain_=terrain; brushMode_=mode; brushRadius_=radius; brushStrength_=strength;
    brushLayer_=layer; brushFlat_=flatHeight; brushStroke_=false; brushHitValid_=false; update();
}
void Renderer::updateTerrainBrush(const QPoint& position,bool apply)
{
    if(!brushTerrain_) return;
    auto inverse=(camera.projectionMatrix(width(),height())*camera.viewMatrix()).inverted();
    auto target=inverse.map(QVector3D(2.f*position.x()/std::max(width(),1)-1,1-2.f*position.y()/std::max(height(),1),1));
    brushHitValid_=brushTerrain_->raycast(camera.position,(target-camera.position).normalized(),brushHit_);
    if(apply && brushHitValid_)
    {
        SceneObject* owner=nullptr;
        for(const auto& object:scene_->getObjects()) if(object->getComponent<TerrainComponent>()==brushTerrain_) owner=object.get();
        if(!owner) return;
        auto* transform=owner->getComponent<TransformComponent>();
        auto local=transform?transform->getModelMatrix().inverted().map(brushHit_):brushHit_;
        brushTerrain_->brush(local.x(),local.z(),brushRadius_,brushStrength_,TerrainComponent::Brush(brushMode_),brushLayer_,brushFlat_);
        sceneDirty_=true;
    }
    update();
}

void Renderer::updateHardwareLighting()
{
    if (!renderSettings.hardwareRayTracing || brushStroke_)
    {
        if (rayScene_)
        {
            hardware_->cancel();
            rayScene_.reset();
            latestRayFrame_ = {};
            post_.resetLighting();
        }
        return;
    }
    auto screenView = camera.projectionMatrix(width(), height()) * camera.viewMatrix();
    if (rayScene_ && screenView != motionCamera_) rayMotionClock_.restart();
    motionCamera_ = screenView;
    bool moving = renderSettings.rayFullFrame && rayMotionClock_.isValid() && rayMotionClock_.elapsed() < 150;
    int viewportPixels = std::max(1, int(width() * devicePixelRatioF()));
    int maximum = std::min(std::clamp(renderSettings.rayWidth, 160, 1920), viewportPixels);
    if (adaptiveRayWidth_ <= 0) adaptiveRayWidth_ = maximum;
    int targetWidth = std::min(maximum, adaptiveRayWidth_);
    if (moving) targetWidth = std::min(targetWidth, std::max(160, maximum / 2));
    int targetHeight = std::max(1, int(float(targetWidth) * height() / std::max(width(), 1)));
    if (targetHeight > 1080)
    {
        targetWidth = std::max(1, int(float(targetWidth) * 1080 / targetHeight));
        targetHeight = 1080;
    }
    QSize size(targetWidth, targetHeight);
    auto view = camera.projectionMatrix(size.width(), size.height()) * camera.viewMatrix();
    // Display-only controls must not discard converged GPU samples.
    auto allSettings=renderSettings.toJson();QJsonObject settings;
    const QStringList traceKeys={"raySamples","rayWidth","rayBudgetMs","rayFullFrame","rayBounces","rayDenoise",
        "rayDenoisePasses","rayMovingSamples","rayDenoiseStrength","rayIndirectClamp","rayGI","raySunIntensity","raySkyIntensity","raySunAngle"};
    for(const auto& key:traceKeys)settings.insert(key,allSettings.value(key));
    settings.insert("moving",moving);settings.insert("maximum",maximum);
    if (raySceneDirty_ || !rayScene_ || view != rayCamera_ || size != raySize_ || settings != raySettings_)
    {
        if (raySceneDirty_ || !rayScene_)
        {
            post_.resetLighting();
            latestRayFrame_ = {};
            rayScene_ = std::make_shared<RaySceneSnapshot>(RaySceneSnapshot::capture(*scene_));
        }
        else if (settings != raySettings_ && settings["moving"] == raySettings_["moving"])
        {
            hardware_->cancel();
            post_.resetLighting();
            latestRayFrame_ = {};
        }
        raySceneDirty_ = false;
        rayCamera_ = view; raySize_ = size; raySettings_ = settings; rayProgress_ = 0;
        HardwareRaySettings quality;
        quality.samples = std::clamp(renderSettings.raySamples, 1, 512);
        quality.bounces = renderSettings.rayBounces;
        quality.fullFrame = renderSettings.rayFullFrame;
        quality.moving = moving;
        quality.denoise=renderSettings.rayDenoise;
        quality.denoisePasses=renderSettings.rayDenoisePasses;
        quality.movingSamples=renderSettings.rayMovingSamples;
        quality.denoiseStrength=renderSettings.rayDenoiseStrength;
        quality.indirectClamp=renderSettings.rayIndirectClamp;
        quality.globalIllumination=renderSettings.rayGI;
        quality.sunIntensity=renderSettings.raySunIntensity;
        quality.skyIntensity=renderSettings.raySkyIntensity;
        quality.sunAngle=renderSettings.raySunAngle;
        hardware_->request(rayScene_, camera, size.width(), size.height(), quality);
    }
}
void Renderer::drawRenderStats(QPainter& painter)
{
    if (!renderSettings.hardwareRayTracing) return;
    painter.fillRect(QRect(8, 8, 420, 78), QColor(12, 18, 26, 210));
    painter.setPen(QColor(110, 230, 205));
    QString mode = renderSettings.rayFullFrame ? "LIVE PATH TRACING" : "HYBRID LIGHTING";
    painter.drawText(16, 28, QString("%1  |  viewport %2 fps").arg(mode).arg(rasterFps_, 0, 'f', 0));
    painter.setPen(Qt::white);
    if (latestRayFrame_.width)
    {
        painter.drawText(16, 49, QString("RT %1 × %2 · %3 ms · samples %4 · uploads %5")
            .arg(latestRayFrame_.width).arg(latestRayFrame_.height)
            .arg(latestRayFrame_.renderMilliseconds, 0, 'f', 1)
            .arg(latestRayFrame_.historySamples).arg(latestRayFrame_.sceneUploads));
        if (latestRayFrame_.gpuMilliseconds >= 0)
            painter.drawText(16, 70, QString("GPU trace + filter %1 ms · %2")
                .arg(latestRayFrame_.gpuMilliseconds, 0, 'f', 2)
                .arg(renderSettings.rayFullFrame ? QString("%1 bounces · %2").arg(renderSettings.rayBounces).arg(latestRayFrame_.moving ? "moving" : "refining")
                                               : "1 diffuse + selective reflection ray/pixel"));
    }
    else painter.drawText(16, 49, "Preparing hardware lighting · raster viewport stays live");
}

void Renderer::focusObject(SceneObject* object)
{
    if(!object) return;
    auto* mesh=object->getComponent<MeshComponent>();
    if(!mesh||!mesh->mesh)
    {auto* t=object->getComponent<TransformComponent>();if(t)camera.position=t->getModelMatrix().map(QVector3D())-camera.forward()*5;update();return;}
    auto* transform=object->getComponent<TransformComponent>();auto model=transform?transform->getModelMatrix():QMatrix4x4();
    QVector3D minimum(1e30f,1e30f,1e30f),maximum(-1e30f,-1e30f,-1e30f);
    const auto& values=mesh->mesh->vertices;
    for(size_t i=0;i+7<values.size();i+=8)
    {
        auto p=model.map(QVector3D(values[i],values[i+1],values[i+2]));
        for(int a=0;a<3;++a){minimum[a]=std::min(minimum[a],p[a]);maximum[a]=std::max(maximum[a],p[a]);}
    }
    if(values.empty())return;
    auto center=(minimum+maximum)*0.5f;float radius=std::max((maximum-minimum).length()*0.5f,1.f);
    camera.nearPlane=std::max(0.05f,radius*0.001f);camera.farPlane=std::max(500.f,radius*8.f);
    camera.position=center+QVector3D(1,0.8f,1).normalized()*radius*2.4f;
    auto direction=(center-camera.position).normalized();
    camera.yaw_=qRadiansToDegrees(std::atan2(direction.z(),direction.x()));camera.pitch_=qRadiansToDegrees(std::asin(direction.y()));update();
}

void Renderer::setGizmoMode(int mode)
{
    // Finish the current operation before changing handles (the center scale
    // handle has no corresponding rotation or translation axis).
    if(gizmo_.dragging())
    {if(gizmo_.finish())scene_->recordEdit();invalidateLighting();emit transformEdited();}
    gizmo_.mode=TransformGizmo::Mode(std::clamp(mode,0,2));update();
}
void Renderer::setSelectedObject(SceneObject* object)
{
    if(gizmo_.object==object)return;
    if(gizmo_.dragging()){if(gizmo_.finish())scene_->recordEdit();emit transformEdited();}
    gizmo_.object=object;update();
}
void Renderer::drawEditorTools(QPainter& painter)
{
    painter.setRenderHint(QPainter::Antialiasing);
    auto vp=camera.projectionMatrix(width(),height())*camera.viewMatrix();
    for(const auto& object:scene_->getObjects())
    {
        auto* component=object->getComponent<LightComponent>();if(!component)continue;
        auto light=component->worldLight();QPointF center;
        if(!TransformGizmo::project(light.position,vp,size(),center))continue;
        QColor color=component->enabled?QColor::fromRgbF(light.color.x(),light.color.y(),light.color.z()):QColor(100,100,100);
        painter.setPen(QPen(color,gizmo_.object==object.get()?3:2));painter.setBrush(Qt::NoBrush);
        if(light.type==3)painter.drawRect(QRectF(center-QPointF(8,6),QSizeF(16,12)));
        else painter.drawEllipse(center,6,6);
        if(light.type==0)for(int ray=0;ray<8;++ray)
        {float angle=ray*3.14159265f/4;QPointF direction(std::cos(angle),std::sin(angle));painter.drawLine(center+direction*9,center+direction*14);}
        if(gizmo_.object==object.get())
        {
            painter.drawText(center+QPointF(15,20),QString::fromStdString(object->name()));
            QPointF end;if(light.type!=1&&TransformGizmo::project(light.position+light.direction*2,vp,size(),end))painter.drawLine(center,end);
        }
    }
    if(gizmo_.object)
    {
        painter.setPen(QPen(QColor(255,210,75,180),1));painter.setBrush(Qt::NoBrush);
        auto line=[&](QVector3D a,QVector3D b){QPointF first,last;if(TransformGizmo::project(a,vp,size(),first)&&TransformGizmo::project(b,vp,size(),last))painter.drawLine(first,last);};
        auto* light=gizmo_.object->getComponent<LightComponent>();
        if(light)
        {
            auto l=light->worldLight();
            auto ring=[&](QVector3D center,QVector3D right,QVector3D up,float radius)
            {for(int i=0;i<48;++i){float a=i*2*M_PI/48,b=(i+1)*2*M_PI/48;line(center+(right*std::cos(a)+up*std::sin(a))*radius,center+(right*std::cos(b)+up*std::sin(b))*radius);}};
            if(l.type==1){ring(l.position,{1,0,0},{0,1,0},l.range);ring(l.position,{1,0,0},{0,0,1},l.range);ring(l.position,{0,1,0},{0,0,1},l.range);}
            if(l.type==2)
            {
                float distance=l.range,radius=std::tan(qDegreesToRadians(l.outerCone))*distance;
                auto end=l.position+l.direction*distance;ring(end,l.right,l.up,radius);
                for(auto offset:{l.right*radius,-l.right*radius,l.up*radius,-l.up*radius})line(l.position,end+offset);
            }
            if(l.type==3)
            {
                auto right=l.right*l.width*.5f,up=l.up*l.height*.5f;
                line(l.position-right-up,l.position+right-up);line(l.position+right-up,l.position+right+up);
                line(l.position+right+up,l.position-right+up);line(l.position-right+up,l.position-right-up);
            }
        }
        else if(auto* mesh=gizmo_.object->getComponent<MeshComponent>();mesh&&mesh->mesh&&!mesh->mesh->vertices.empty())
        {
            QVector3D low(1e30f,1e30f,1e30f),high=-low;const auto& vertices=mesh->mesh->vertices;
            for(size_t i=0;i+7<vertices.size();i+=8)for(int axis=0;axis<3;++axis){low[axis]=std::min(low[axis],vertices[i+axis]);high[axis]=std::max(high[axis],vertices[i+axis]);}
            auto* t=gizmo_.object->getComponent<TransformComponent>();auto model=t?t->getModelMatrix():QMatrix4x4();
            QVector3D corners[8];for(int i=0;i<8;++i)corners[i]=model.map(QVector3D(i&1?high.x():low.x(),i&2?high.y():low.y(),i&4?high.z():low.z()));
            for(int i=0;i<8;++i)for(int axis=0;axis<3;++axis)if(!(i&(1<<axis)))line(corners[i],corners[i|(1<<axis)]);
        }
    }
    gizmo_.paint(painter,vp,size());
}
SceneObject* Renderer::pickObject(const QPoint& mouse)
{
    auto vp=camera.projectionMatrix(width(),height())*camera.viewMatrix();
    auto target=vp.inverted().map(QVector3D(2.f*mouse.x()/std::max(width(),1)-1,1-2.f*mouse.y()/std::max(height(),1),1));
    auto direction=(target-camera.position).normalized();
    SceneObject* selected=nullptr;float nearest=1e30f;
    for(const auto& object:scene_->getObjects())
    {
        auto* transform=object->getComponent<TransformComponent>();auto model=transform?transform->getModelMatrix():QMatrix4x4();
        if(object->getComponent<LightComponent>())
        {QPointF center;if(TransformGizmo::project(model.map(QVector3D()),vp,size(),center)&&QLineF(mouse,center).length()<16)return object.get();}
        auto* mesh=object->getComponent<MeshComponent>();if(!mesh||!mesh->mesh)continue;
        const auto& vertices=mesh->mesh->vertices;const auto& indices=mesh->mesh->indices;
        for(size_t i=0;i+2<indices.size();i+=3)
        {
            auto point=[&](unsigned int index){size_t j=size_t(index)*8;return model.map(QVector3D(vertices[j],vertices[j+1],vertices[j+2]));};
            if(size_t(std::max({indices[i],indices[i+1],indices[i+2]}))*8+2>=vertices.size())continue;
            auto a=point(indices[i]),edge1=point(indices[i+1])-a,edge2=point(indices[i+2])-a;
            auto cross=QVector3D::crossProduct(direction,edge2);float determinant=QVector3D::dotProduct(edge1,cross);
            if(std::abs(determinant)<.0000001f)continue;
            auto offset=camera.position-a;float u=QVector3D::dotProduct(offset,cross)/determinant;if(u<0||u>1)continue;
            auto q=QVector3D::crossProduct(offset,edge1);float v=QVector3D::dotProduct(direction,q)/determinant;if(v<0||u+v>1)continue;
            float distance=QVector3D::dotProduct(edge2,q)/determinant;
            if(distance>0&&distance<nearest){nearest=distance;selected=object.get();}
        }
    }
    return selected;
}
