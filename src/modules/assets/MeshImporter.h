#pragma once

#include "../componentsBackend/MeshComponent/Mesh.h"
#include <filesystem>
#include <memory>

class MeshImporter
{
public:
    // OBJ geometry only: triangles and convex polygons. Throws on invalid input.
    // Materials, smoothing groups, and object/group boundaries are not imported.
    static std::unique_ptr<Mesh> load(const std::filesystem::path& path);
};
