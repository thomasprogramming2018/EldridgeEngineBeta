#pragma once
#include "camera.h"
#include <QImage>
#include <QVector2D>
#include <atomic>
#include <functional>
#include <vector>

class Scene;
struct RayMaterial
{
    QVector3D color {0.8f, 0.6f, 0.4f};
    QVector3D emission {0, 0, 0};
    float metallic = 0;
    float roughness = 0.5f;
    bool lit = true;
    QImage texture;
    QImage normalTexture;
    QImage metallicTexture;
    QImage roughnessTexture;
    float clearCoat = 0;
    float normalStrength = 1;
};
struct RayTriangle
{
    QVector3D a, b, c;
    QVector3D na, nb, nc;
    QVector2D uvA, uvB, uvC;
    int material = 0;
};

// A CPU triangle ray tracer. A BVH avoids testing every triangle for every ray.
// It reads copied scene data and never touches editor objects from its thread.
class SoftwareRayTracer
{
public:
    void capture(const Scene& scene, bool buildCpuBvh = true);
    QImage render(const Camera& camera, int width, int height, int samples,
                  std::atomic<bool>& cancel, const std::function<void(int)>& progress);
    const auto& triangles() const { return triangles_; }
    const auto& materials() const { return materials_; }
    int triangleCount() const { return int(triangles_.size()); }
private:
    struct Node { QVector3D minimum, maximum; int start, count, left = -1, right = -1; };
    struct Hit { float distance; int triangle = -1; float u = 0, v = 0; };
    int build(int start, int count);
    bool hitBounds(const Node& node, const QVector3D& origin, const QVector3D& direction, float limit) const;
    Hit intersect(const QVector3D& origin, const QVector3D& direction, float limit) const;
    QVector3D shade(const QVector3D& origin, const QVector3D& direction, int bounce) const;
    std::vector<RayTriangle> triangles_;
    std::vector<RayMaterial> materials_;
    std::vector<Node> nodes_;
};
