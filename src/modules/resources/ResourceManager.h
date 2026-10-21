#pragma once
#include <QHash>
#include <QString>
#include <QOpenGLTexture>
#include <memory>
#include "../shader/Shader.h"

// The viewport owns ResourceManager and calls its GPU methods only while GL is current.
class ResourceManager {
public:
    std::shared_ptr<Shader> loadShader(const QString& key, const ShaderDescription& description);
    std::shared_ptr<Shader> findShader(const QString& key) const;
    std::shared_ptr<Shader> loadShaderAsset(const QString& path, QString& error);
    QOpenGLTexture* texture(const QString& path);
    QOpenGLTexture* whiteTexture();
    void releaseGL();
private:
    QHash<QString, std::shared_ptr<Shader>> shaders_;
    std::vector<std::shared_ptr<Shader>> assetShaders_;
    QHash<QString, std::shared_ptr<QOpenGLTexture>> textures_;
    std::shared_ptr<QOpenGLTexture> white_;
};
