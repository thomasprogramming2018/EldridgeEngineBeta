#include "modules/shader/ShaderAsset.h"
#include "modules/shader/Shader.h"
#include "modules/material/Material.h"
#include "modules/resources/ResourceManager.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QTemporaryDir>
#include <QFile>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    try
    {
        QTemporaryDir directory;
        QString path = directory.filePath("Example.elsha");
        ShaderAsset asset;
        require(asset.load(QString(ENGINE_SHADER_DIRECTORY) + "/Lit.elsha"), "Load Lit asset");
        asset.name = "Round trip";
        asset.affectedByLights = false;
        require(asset.save(path), "Save .elsha");
        ShaderAsset loaded;
        require(loaded.load(path), "Load saved .elsha");
        require(loaded.name == asset.name && loaded.vertexSource == asset.vertexSource &&
                loaded.fragmentSource == asset.fragmentSource && !loaded.affectedByLights,
                "Source and rendering settings survive saving");
        require(!asset.save(directory.filePath("Wrong.json")), "Require .elsha extension");
        require(Material::isEngineUniform("lights[3].diffuse"), "Hide light fields");
        require(Material::isEngineUniform("dlight.dir"), "Hide directional light");
        require(Material::isEngineUniform("uTime"), "Hide engine time");
        require(Material::isEngineUniform("uAffectedByLights"), "Hide rendering switch");
        require(!Material::isEngineUniform("material.albedo"), "Keep material controls");

        QSurfaceFormat format;
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        QOpenGLContext context;
        context.setFormat(format);
        require(context.create(), "Create test OpenGL context");
        QOffscreenSurface surface;
        surface.setFormat(context.format());
        surface.create();
        require(context.makeCurrent(&surface), "Make test OpenGL context current");

        ResourceManager resources;
        QString error;
        auto shader = resources.loadShaderAsset(path, error);
        require(bool(shader), "Compile saved shader");
        require(!shader->affectedByLights(), "Apply unlit rendering setting");
        Material material;
        material.setShader(shader);
        require(!material.parameters.contains("uAffectedByLights"), "No engine switch control");
        require(material.parameters.contains("uBaseColor"), "Reflect user controls");

        asset.affectedByLights = true;
        require(asset.save(path), "Save changed rendering setting");
        auto reloaded = resources.loadShaderAsset(path, error);
        require(reloaded && reloaded->affectedByLights(), "Reload same path without stale cache");
        asset.fragmentSource = "invalid shader";
        require(asset.save(path), "Save invalid GLSL for compile check");
        require(!resources.loadShaderAsset(path, error) && !error.isEmpty(), "Report compile failure");
        require(material.shader == shader && shader->bind(), "Failed load preserves working material");

        for (const QString& name : {QString("Lit"), QString("Unlit"), QString("PBR"), QString("HDRP"), QString("Terrain")})
        {
            auto builtin = resources.loadShaderAsset(QString(ENGINE_SHADER_DIRECTORY) + "/" + name + ".elsha", error);
            require(bool(builtin), "Compile built-in asset");
            material.setShader(builtin);
            for (auto it = material.parameters.begin(); it != material.parameters.end(); ++it)
                require(!Material::isEngineUniform(it.key()), "Engine values excluded from material");
        }
        resources.releaseGL();
        context.doneCurrent();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
