#pragma once

#include <memory>

#include "Mesh.h"
#include "../component.h"
#include <filesystem>
#include <string>






class SceneObject;




class MeshComponent : public Component

{
public:
    explicit MeshComponent(SceneObject& owner)
        : Component(owner) {}

    virtual ~MeshComponent() = default;





    enum class Source { None, Cube, Imported, Terrain };
    Source source() const { return source_; }
    const std::string& assetPath() const { return assetPath_; }

    // Restore saved mesh data without importing the original file again.
    void restoreMesh(std::unique_ptr<Mesh> data, Source source, const std::string& path)
    {
        mesh = std::move(data);
        source_ = source;
        assetPath_ = path;
    }

    void createCube();
    void importMesh(const std::filesystem::path& path);

    void createMesh() {}

    void createPlane() {}

    std::unique_ptr<Mesh> mesh;

private:
    Source source_ = Source::None;
    std::string assetPath_;
};
