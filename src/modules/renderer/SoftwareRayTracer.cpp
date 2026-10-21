#include "SoftwareRayTracer.h"
#include "../componentsBackend/TerrainComponent/TerrainComponent.h"
#include "../scene/Scene.h"
#include "../componentsBackend/MeshComponent/MeshComponent.h"
#include "../componentsBackend/TransformComponent/TransformComponent.h"
#include "../material/MaterialComponent.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace
{
QVector3D multiply(const QVector3D& a, const QVector3D& b) { return {a.x()*b.x(), a.y()*b.y(), a.z()*b.z()}; }
float dot(const QVector3D& a, const QVector3D& b) { return QVector3D::dotProduct(a, b); }
QVector3D reflect(const QVector3D& direction, const QVector3D& normal) { return direction - 2.f * dot(direction, normal) * normal; }
QVector3D sky(const QVector3D& direction)
{
    float t = std::clamp(direction.y() * 0.5f + 0.5f, 0.f, 1.f);
    return QVector3D(0.07f, 0.09f, 0.13f) * (1-t) + QVector3D(0.35f, 0.5f, 0.8f) * t;
}
float scalar(const Material& material, const QString& key, float fallback)
{
    const auto* value = material.get(key);
    if (value) if (auto* number = std::get_if<float>(value)) return *number;
    return fallback;
}
}
void SoftwareRayTracer::capture(const Scene& scene, bool buildCpuBvh)
{
    triangles_.clear(); materials_.clear(); nodes_.clear();
    std::map<const Material*, int> capturedMaterials;
    for (const auto& object : scene.getObjects())
    {
        auto* mesh = object->getComponent<MeshComponent>();
        auto* material = object->getComponent<MaterialComponent>();
        auto* transform = object->getComponent<TransformComponent>();
        if (!mesh || !mesh->mesh || !material || !material->material) continue;
        auto& source = *material->material;
        auto* terrain = object->getComponent<TerrainComponent>();
        auto known = capturedMaterials.find(&source);
        int materialIndex = known == capturedMaterials.end() ? -1 : known->second;
        if (terrain || materialIndex < 0)
        {
            RayMaterial value;
            for (const QString& name : {QString("material.albedo"), QString("uBaseColor")})
                if (auto* parameter = source.get(name))
                    if (auto* color = std::get_if<QVector3D>(parameter)) value.color = *color;
            if (auto* parameter = source.get("material.emissive"))
                if (auto* color = std::get_if<QVector3D>(parameter)) value.emission = *color;
            value.metallic = std::clamp(scalar(source, "material.metallic", 0.f), 0.f, 1.f);
            value.roughness = std::clamp(scalar(source, "material.roughness", scalar(source,"uRoughness",0.5f)), 0.04f, 1.f);
            if (source.shader) value.lit = source.shader->affectedByLights();
            for (const QString& name : {QString("material.diffuse"), QString("uAlbedo")})
            {
                bool enabled = false;
                if (name == "material.diffuse")
                {
                    if (auto* flag = source.get("material.hasDiffuseMap"))
                        if (auto* number = std::get_if<int>(flag)) enabled = *number != 0;
                }
                else if (auto* flag = source.get("uUseTexture"))
                    if (auto* toggle = std::get_if<bool>(flag)) enabled = *toggle;
                if (enabled) if (auto* parameter = source.get(name))
                    if (auto* path = std::get_if<TexturePath>(parameter)) value.texture.load(path->path);
            }
            value.clearCoat = scalar(source, "material.clearCoat", 0.f);
            value.normalStrength = scalar(source, "material.normalStrength", 1.f);
            const QString maps[] = {"material.normal", "material.metallic_tex", "material.roughness_tex"};
            const QString flags[] = {"material.hasNormalMap", "material.hasMetallicMap", "material.hasRoughnessMap"};
            QImage* images[] = {&value.normalTexture, &value.metallicTexture, &value.roughnessTexture};
            for (int map = 0; map < 3; ++map)
            {
                if (auto* flag = source.get(flags[map])) if (auto* enabled = std::get_if<int>(flag))
                    if (*enabled) if (auto* parameter = source.get(maps[map]))
                        if (auto* path = std::get_if<TexturePath>(parameter)) images[map]->load(path->path);
            }
            if (terrain)
            {
                value.texture=terrain->bakeAlbedo();
                value.color={1,1,1}; value.roughness=0.8f;
            }
            materialIndex = int(materials_.size());
            materials_.push_back(value);
            if (!terrain) capturedMaterials[&source] = materialIndex;
        }
        auto model = transform ? transform->getModelMatrix() : QMatrix4x4();
        auto normalMatrix = model.inverted().transposed();
        const auto& vertices = mesh->mesh->vertices;
        const auto& indices = mesh->mesh->indices;
        for (int i = 0; i + 2 < int(indices.size()); i += 3)
        {
            RayTriangle triangle;
            QVector3D* points[] = {&triangle.a, &triangle.b, &triangle.c};
            QVector3D* normals[] = {&triangle.na, &triangle.nb, &triangle.nc};
            QVector2D* uvs[] = {&triangle.uvA, &triangle.uvB, &triangle.uvC};
            bool valid = true;
            for (int corner = 0; corner < 3; ++corner)
            {
                size_t base = size_t(indices[i+corner]) * 8;
                if (base + 7 >= vertices.size()) { valid = false; break; }
                *points[corner] = model.map(QVector3D(vertices[base], vertices[base+1], vertices[base+2]));
                *normals[corner] = normalMatrix.mapVector(QVector3D(vertices[base+3], vertices[base+4], vertices[base+5])).normalized();
                *uvs[corner] = QVector2D(vertices[base+6], vertices[base+7]);
            }
            triangle.material = materialIndex;
            if (valid) triangles_.push_back(triangle);
        }
    }
    if (buildCpuBvh)
    {
        nodes_.reserve(triangles_.size() * 2);
        if (!triangles_.empty()) build(0, triangles_.size());
    }
}
int SoftwareRayTracer::build(int start, int count)
{
    Node node;
    node.start = start; node.count = count;
    node.minimum = QVector3D(1e30f,1e30f,1e30f);
    node.maximum = -node.minimum;
    for (int i = start; i < start + count; ++i)
        for (const auto& point : {triangles_[i].a, triangles_[i].b, triangles_[i].c})
            for (int axis = 0; axis < 3; ++axis)
            { node.minimum[axis] = std::min(node.minimum[axis], point[axis]); node.maximum[axis] = std::max(node.maximum[axis], point[axis]); }
    int index = nodes_.size(); nodes_.push_back(node);
    if (count > 8)
    {
        QVector3D size = node.maximum - node.minimum;
        int axis = size.x() > size.y() ? 0 : 1;
        if (size.z() > size[axis]) axis = 2;
        int middle = start + count / 2;
        std::nth_element(triangles_.begin()+start, triangles_.begin()+middle, triangles_.begin()+start+count,
            [axis](const RayTriangle& a, const RayTriangle& b)
            { return a.a[axis]+a.b[axis]+a.c[axis] < b.a[axis]+b.b[axis]+b.c[axis]; });
        nodes_[index].left = build(start, count/2);
        nodes_[index].right = build(middle, count-count/2);
    }
    return index;
}
bool SoftwareRayTracer::hitBounds(const Node& node, const QVector3D& origin, const QVector3D& direction, float limit) const
{
    float near = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(direction[axis]) < 1e-8f)
        { if (origin[axis] < node.minimum[axis] || origin[axis] > node.maximum[axis]) return false; continue; }
        float a = (node.minimum[axis]-origin[axis])/direction[axis];
        float b = (node.maximum[axis]-origin[axis])/direction[axis];
        if (a>b) std::swap(a,b);
        near = std::max(near,a); limit = std::min(limit,b);
        if (near>limit) return false;
    }
    return true;
}
SoftwareRayTracer::Hit SoftwareRayTracer::intersect(const QVector3D& origin, const QVector3D& direction, float limit) const
{
    Hit hit; hit.distance = limit;
    if (nodes_.empty()) return hit;
    std::vector<int> stack {0};
    while (!stack.empty())
    {
        int index = stack.back(); stack.pop_back();
        const auto& node = nodes_[index];
        if (!hitBounds(node,origin,direction,hit.distance)) continue;
        if (node.left >= 0) { stack.push_back(node.left); stack.push_back(node.right); continue; }
        for (int i = node.start; i < node.start+node.count; ++i)
        {
            const auto& triangle = triangles_[i];
            auto edge1 = triangle.b-triangle.a, edge2 = triangle.c-triangle.a;
            auto p = QVector3D::crossProduct(direction,edge2);
            float determinant = dot(edge1,p);
            if (std::abs(determinant)<1e-7f) continue;
            float inverse = 1.f/determinant;
            auto offset = origin-triangle.a;
            float u = dot(offset,p)*inverse;
            if (u<0 || u>1) continue;
            auto q = QVector3D::crossProduct(offset,edge1);
            float v = dot(direction,q)*inverse;
            if (v<0 || u+v>1) continue;
            float distance = dot(edge2,q)*inverse;
            if (distance>0.001f && distance<hit.distance) hit = {distance,i,u,v};
        }
    }
    return hit;
}
QVector3D SoftwareRayTracer::shade(const QVector3D& origin, const QVector3D& direction, int bounce) const
{
    Hit hit = intersect(origin,direction,1e30f);
    if (hit.triangle<0) return sky(direction);
    const auto& triangle = triangles_[hit.triangle];
    const auto& material = materials_[triangle.material];
    auto point = origin + direction*hit.distance;
    auto normal = (triangle.na*(1-hit.u-hit.v)+triangle.nb*hit.u+triangle.nc*hit.v).normalized();
    if (dot(normal,direction)>0) normal = -normal;
    QVector3D color = material.color;
    if (!material.texture.isNull())
    {
        auto uv = triangle.uvA*(1-hit.u-hit.v)+triangle.uvB*hit.u+triangle.uvC*hit.v;
        float u = uv.x()-std::floor(uv.x()), v = uv.y()-std::floor(uv.y());
        auto texel = material.texture.pixelColor(std::min(int(u*material.texture.width()),material.texture.width()-1),
            std::min(int((1-v)*material.texture.height()),material.texture.height()-1));
        color = multiply(color,QVector3D(std::pow(texel.redF(),2.2f),std::pow(texel.greenF(),2.2f),std::pow(texel.blueF(),2.2f)));
    }
    if (!material.lit) return color + material.emission;
    auto light = QVector3D(0.4f,1.f,0.3f).normalized();
    bool shadow = intersect(point+normal*0.003f,light,1e30f).triangle>=0;
    float diffuse = std::max(dot(normal,light),0.f) * (shadow ? 0.f : 1.f);
    auto tangent = QVector3D::crossProduct(normal, std::abs(normal.y())<0.9f ? QVector3D(0,1,0):QVector3D(1,0,0)).normalized();
    auto bitangent = QVector3D::crossProduct(normal,tangent);
    float ambient = 1.f;
    for (int i = 0; i<4; ++i)
    {
        float a = float(i)*2.39996f;
        auto ray = (normal+0.7f*(std::cos(a)*tangent+std::sin(a)*bitangent)).normalized();
        if (intersect(point+normal*0.003f,ray,0.8f).triangle>=0) ambient -= 0.18f;
    }
    auto half = (light-direction).normalized();
    float highlight = std::pow(std::max(dot(normal,half),0.f), 2.f/(material.roughness*material.roughness));
    QVector3D result = color * (0.15f*ambient + diffuse*(1-material.metallic)) + material.emission;
    if (!shadow) result += QVector3D(1,1,1)*highlight*0.15f*(1-material.roughness);
    if (bounce<2)
    {
        float fresnel = 0.04f + 0.96f*std::pow(1-std::max(dot(normal,-direction),0.f),5.f);
        float weight = std::clamp((material.metallic+(1-material.metallic)*fresnel)*(1-material.roughness),0.f,0.95f);
        auto reflection = shade(point+normal*0.003f,reflect(direction,normal).normalized(),bounce+1);
        result = result*(1-weight)+multiply(reflection,material.metallic>0.5f?color:QVector3D(1,1,1))*weight;
    }
    return result;
}
QImage SoftwareRayTracer::render(const Camera& camera, int width, int height, int samples,
    std::atomic<bool>& cancel, const std::function<void(int)>& progress)
{
    width = std::clamp(width,1,1920); height = std::clamp(height,1,1080); samples = std::clamp(samples,1,16);
    QImage image(width,height,QImage::Format_RGB32);
    auto inverse = (camera.projectionMatrix(width,height)*camera.viewMatrix()).inverted();
    for (int y=0; y<height; ++y)
    {
        if (cancel) return {};
        for (int x=0; x<width; ++x)
        {
            QVector3D color;
            for (int sample=0; sample<samples; ++sample)
            {
                float jitterX = (sample+0.5f)/samples;
                float jitterY = std::fmod(sample*0.618034f+0.5f,1.f);
                auto target = inverse.map(QVector3D(2.f*(x+jitterX)/width-1,1-2.f*(y+jitterY)/height,1));
                color += shade(camera.position,(target-camera.position).normalized(),0);
            }
            color /= samples;
            for (int axis=0; axis<3; ++axis)
            {
                float v = std::max(color[axis],0.f);
                v = std::clamp((v*(2.51f*v+0.03f))/(v*(2.43f*v+0.59f)+0.14f),0.f,1.f);
                color[axis] = std::pow(v,1.f/2.2f);
            }
            image.setPixelColor(x,y,QColor::fromRgbF(color.x(),color.y(),color.z()));
        }
        if (progress) progress((y+1)*100/height);
    }
    return image;
}
