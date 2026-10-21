#include "LightingDemo.h"
#include "../componentsBackend/LightComponent/LightComponent.h"
#include "../scene/Scene.h"
#include "../componentsBackend/TransformComponent/TransformComponent.h"
#include "../componentsBackend/MeshComponent/MeshComponent.h"
#include "../material/MaterialComponent.h"
#include "../renderer/camera.h"
#include <cmath>

namespace
{
std::shared_ptr<Material> makeMaterial(QVector3D color, float metallic, float roughness, QVector3D emission = {})
{
    auto material = std::make_shared<Material>();
    material->shaderPath = QString(ENGINE_SHADER_DIRECTORY) + "/HDRP.elsha";
    material->parameters["material.albedo"] = color;
    material->parameters["material.metallic"] = metallic;
    material->parameters["material.roughness"] = roughness;
    material->parameters["material.specular_color"] = QVector3D(0.04f, 0.04f, 0.04f);
    material->parameters["material.normal_vector"] = QVector3D(0, 0, 1);
    material->parameters["material.normalStrength"] = 1.f;
    material->parameters["material.clearCoat"] = 0.25f;
    material->parameters["material.emissive"] = emission;
    return material;
}
std::unique_ptr<Mesh> sphere()
{
    auto mesh = std::make_unique<Mesh>();
    const int rings = 24, segments = 48;
    const float pi = 3.14159265f;
    for (int y = 0; y <= rings; ++y) for (int x = 0; x <= segments; ++x)
    {
        float u = float(x) / segments, v = float(y) / rings;
        QVector3D normal(std::sin(v*pi) * std::cos(u*2*pi), std::cos(v*pi), std::sin(v*pi) * std::sin(u*2*pi));
        mesh->vertices.insert(mesh->vertices.end(), {normal.x(), normal.y(), normal.z(), normal.x(), normal.y(), normal.z(), u, v});
    }
    for (int y = 0; y < rings; ++y) for (int x = 0; x < segments; ++x)
    {
        unsigned int a = y * (segments + 1) + x, b = a + segments + 1;
        // Counter-clockwise faces point outward.
        if (y > 0) mesh->indices.insert(mesh->indices.end(), {a, a + 1, b});
        if (y < rings - 1) mesh->indices.insert(mesh->indices.end(), {a + 1, b + 1, b});
    }
    return mesh;
}
}
void LightingDemo::populate(Scene& scene)
{
    auto box = [&](const char* name, QVector3D position, QVector3D scale, std::shared_ptr<Material> material)
    {
        auto& object = scene.createCube(name);
        auto* transform = object.getComponent<TransformComponent>();
        transform->position = position; transform->scale = scale;
        object.addComponent<MaterialComponent>().material = material;
    };
    auto neutral = makeMaterial({0.48f, 0.5f, 0.55f}, 0, 0.8f);
    auto dark = makeMaterial({0.08f, 0.09f, 0.12f}, 0.35f, 0.45f);
    box("Stone floor", {0, -0.15f, 0}, {10, 0.3f, 10}, neutral);
    box("Back wall", {0, 2.5f, -4.5f}, {10, 5, 0.2f}, neutral);
    box("Left wall", {-4.5f, 2.5f, 0}, {0.2f, 5, 9}, neutral);
    box("Magenta emission", {-4.36f, 1.8f, -0.5f}, {0.05f, 3, 3.5f},
        makeMaterial({0.4f, 0.01f, 0.12f}, 0, 0.6f, {6, 0.08f, 1.5f}));
    box("Cyan emission", {3.8f, 1.8f, -2.5f}, {0.15f, 3, 2.5f},
        makeMaterial({0.01f, 0.3f, 0.4f}, 0, 0.6f, {0.05f, 3.5f, 6}));
    box("Amber strip", {0, 0.15f, -4.3f}, {6, 0.12f, 0.1f},
        makeMaterial({0.4f, 0.12f, 0.01f}, 0, 0.5f, {7, 2, 0.1f}));
    for (int i = 0; i < 4; ++i)
        box("Sun shade beam", {0, 4.2f, -3.f + i * 1.3f}, {9, 0.2f, 0.3f}, dark);
    const QVector3D colors[] = {{0.95f, 0.58f, 0.16f}, {0.9f, 0.93f, 0.98f}, {0.7f, 0.16f, 0.08f}};
    const QVector3D positions[] = {{-1.3f, 1, -0.3f}, {1.2f, 1, -0.8f}, {0, 0.55f, 1.5f}};
    const char* names[] = {"Gold sphere", "Chrome sphere", "Ceramic sphere"};
    for (int i = 0; i < 3; ++i)
    {
        auto& object = scene.createObject(names[i]);
        object.addComponent<MeshComponent>().restoreMesh(sphere(), MeshComponent::Source::None, "");
        auto* transform = object.getComponent<TransformComponent>();
        transform->position = positions[i];
        if (i == 2) transform->scale = {0.55f, 0.55f, 0.55f};
        object.addComponent<MaterialComponent>().material = makeMaterial(colors[i], i < 2 ? 1.f : 0.f, i < 2 ? 0.12f : 0.55f);
    }
    auto& sun=scene.createLight(0,"Soft sunlight");
    sun.getComponent<TransformComponent>()->rotation={-65,-25,0};
    auto* sunlight=sun.getComponent<LightComponent>();sunlight->intensity=2;sunlight->angle=3;
    auto& fill=scene.createLight(3,"Overhead softbox");
    fill.getComponent<TransformComponent>()->position={0,3.8f,1};fill.getComponent<TransformComponent>()->rotation={-90,0,0};
    auto* softbox=fill.getComponent<LightComponent>();softbox->width=3;softbox->height=2;softbox->intensity=8;
    scene.recordEdit();
}
void LightingDemo::frame(Camera& camera)
{
    camera.position = {6, 3.8f, 9};
    camera.yaw_ = -123.7f; camera.pitch_ = -14.5f;
    camera.nearPlane = 0.05f; camera.farPlane = 100;
}
