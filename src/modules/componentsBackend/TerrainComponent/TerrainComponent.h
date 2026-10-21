#pragma once
#include "../component.h"
#include "../MeshComponent/Mesh.h"
#include <QImage>
#include <QJsonObject>
#include <QVector3D>
#include <array>

struct TerrainLayer
{
    QString texture;
    QVector3D color;
};
struct TerrainChunk
{
    QVector3D center;
    float radius=0;
    std::array<std::unique_ptr<Mesh>, 4> levels;
};

class TerrainComponent : public Component
{
public:
    enum class Brush { Raise, Lower, Smooth, Flatten, Paint };
    explicit TerrainComponent(SceneObject& owner);

    int resolution = 129; // 2^n + 1. Samples include both edges.
    float size = 64.f;
    float heightRange = 20.f;
    float heightOffset = 0.f;
    float tiling = 16.f;
    std::array<TerrainLayer, 4> layers;
    std::vector<float> heights;
    QImage weights;
    std::vector<TerrainChunk> chunks;
    unsigned int revision = 0;

    void resize(int samples, float worldSize);
    void generate(int seed, float amplitude, float frequency);
    void erode(int iterations, float talus);
    void brush(float x, float z, float radius, float strength, Brush mode, int layer = 0, float flatHeight = 0);
    float heightAt(float x, float z) const;
    bool raycast(const QVector3D& origin, const QVector3D& direction, QVector3D& worldHit) const;
    bool importHeightmap(const QString& path, QString& error);
    bool exportHeightmap(const QString& path, QString& error) const;
    bool importWeights(const QString& path, QString& error);
    bool exportWeights(const QString& path, QString& error) const;
    void rebuild();
    QImage bakeAlbedo(int side=1024) const;
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& data, QString& error);

private:
    std::unique_ptr<Mesh> makeMesh(int x, int z, int cells, int step, bool skirts) const;
    QVector3D normalAt(int x, int z) const;
};
