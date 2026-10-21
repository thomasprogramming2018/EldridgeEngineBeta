#include "ResourceManager.h"
#include <QImage>
#include <QDebug>
#include <QOpenGLContext>

std::shared_ptr<Shader> ResourceManager::loadShader(const QString& key, const ShaderDescription& description)
{
    if (auto cached = findShader(key)) return cached;
    if (!QOpenGLContext::currentContext()) return {};
    auto shader = std::make_shared<Shader>();
    if (!shader->load(description)) {
        qWarning() << shader->log();
        return {};
    }
    shaders_.insert(key, shader);
    return shader;
}
std::shared_ptr<Shader> ResourceManager::findShader(const QString& key) const
{
    return shaders_.value(key);
}
QOpenGLTexture* ResourceManager::texture(const QString& path)
{
    if (path.isEmpty()) return whiteTexture();
    if (textures_.contains(path)) return textures_.value(path).get();
    if (!QOpenGLContext::currentContext()) return whiteTexture();
    QImage image(path);
    if (image.isNull()) {
        qWarning() << "Texture not found:" << path;
        return whiteTexture();
    }
    auto value = std::make_shared<QOpenGLTexture>(image.mirrored());
    value->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
    value->setMagnificationFilter(QOpenGLTexture::Linear);
    value->setWrapMode(QOpenGLTexture::Repeat);
    textures_.insert(path, value);
    return value.get();
}
QOpenGLTexture* ResourceManager::whiteTexture()
{
    if (!white_ && QOpenGLContext::currentContext()) {
        QImage image(1, 1, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        white_ = std::make_shared<QOpenGLTexture>(image);
    }
    return white_.get();
}
void ResourceManager::releaseGL()
{
    // Invoked from viewport cleanup with a current OpenGL context.
    textures_.clear();
    white_.reset();
    for (auto& shader : shaders_) if (shader) shader->release();
    shaders_.clear();
    for (auto& shader : assetShaders_) shader->release();
    assetShaders_.clear();
}

std::shared_ptr<Shader> ResourceManager::loadShaderAsset(const QString& path, QString& error)
{
    ShaderAsset asset;
    if (!asset.load(path))
    {
        error = "Cannot read .elsha shader: " + path;
        return {};
    }
    auto shader = std::make_shared<Shader>();
    if (!shader->loadAsset(asset, path))
    {
        error = shader->log();
        return {};
    }
    // Remove old programs no material uses. OpenGL is current here.
    for (auto it = assetShaders_.begin(); it != assetShaders_.end(); )
    {
        if (it->use_count() == 1)
            it = assetShaders_.erase(it);
        else
            ++it;
    }
    // Keep programs alive until cleanup can release them with OpenGL current.
    assetShaders_.push_back(shader);
    return shader;
}
