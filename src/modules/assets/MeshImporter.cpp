#include "MeshImporter.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using Vec3 = std::array<float, 3>;
using Vec2 = std::array<float, 2>;
struct Corner { Vec3 position; Vec3 normal{}; Vec2 uv{}; bool hasNormal = false; };

std::size_t indexOf(const std::string& text, std::size_t count)
{
    std::size_t consumed = 0;
    const auto index = std::stoll(text, &consumed);
    const auto resolved = index > 0 ? index - 1 : static_cast<long long>(count) + index;
    if (consumed != text.size() || index == 0 || resolved < 0 ||
        static_cast<std::size_t>(resolved) >= count)
        throw std::runtime_error("Invalid OBJ index: " + text);
    return static_cast<std::size_t>(resolved);
}

Vec3 normalized(Vec3 v)
{
    const float length = std::hypot(v[0], v[1], v[2]);
    if (!std::isfinite(length) || length == 0)
        throw std::runtime_error("Zero or invalid normal / degenerate triangle");
    for (auto& value : v) value /= length;
    return v;
}
}

std::unique_ptr<Mesh> MeshImporter::load(const std::filesystem::path& path)
{
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".obj")
        throw std::runtime_error("Unsupported mesh format; expected .obj");
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open mesh: " + path.string());

    auto mesh = std::make_unique<Mesh>();
    std::vector<Vec3> positions, normals;
    std::vector<Vec2> uvs;
    std::string line;
    std::size_t lineNumber = 0;
    try {
        while (std::getline(file, line)) {
            ++lineNumber;
            line = line.substr(0, line.find('#'));
            std::istringstream input(line);
            std::string type;
            input >> type;
            if (type == "v" || type == "vn") {
                Vec3 value;
                for (auto& component : value)
                    if (!(input >> component) || !std::isfinite(component))
                        throw std::runtime_error("Invalid vector");
                if (type == "v") positions.push_back(value);
                else normals.push_back(normalized(value));
            } else if (type == "vt") {
                Vec2 uv{};
                if (!(input >> uv[0]) || !std::isfinite(uv[0]))
                    throw std::runtime_error("Invalid UV");
                input >> std::ws;
                if (!input.eof() && (!(input >> uv[1]) || !std::isfinite(uv[1])))
                    throw std::runtime_error("Invalid UV");
                uvs.push_back(uv);
            } else if (type == "f") {
                std::vector<Corner> face;
                std::string token;
                while (input >> token) {
                    const auto first = token.find('/');
                    const auto second = first == std::string::npos ? first : token.find('/', first + 1);
                    if (second != std::string::npos && token.find('/', second + 1) != std::string::npos)
                        throw std::runtime_error("Invalid face corner");
                    Corner corner;
                    corner.position = positions.at(indexOf(token.substr(0, first), positions.size()));
                    if (first != std::string::npos) {
                        auto uv = token.substr(first + 1, second == std::string::npos ? second : second - first - 1);
                        if (!uv.empty()) corner.uv = uvs.at(indexOf(uv, uvs.size()));
                    }
                    if (second != std::string::npos) {
                        corner.normal = normals.at(indexOf(token.substr(second + 1), normals.size()));
                        corner.hasNormal = true;
                    }
                    face.push_back(corner);
                }
                if (face.size() < 3) throw std::runtime_error("Face needs at least three vertices");
                // Triangle fan supports triangles and convex polygons.
                for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                    std::array<Corner, 3> triangle{face[0], face[i], face[i + 1]};
                    Vec3 a, b;
                    for (int axis = 0; axis < 3; ++axis) {
                        a[axis] = triangle[1].position[axis] - triangle[0].position[axis];
                        b[axis] = triangle[2].position[axis] - triangle[0].position[axis];
                    }
                    const auto normal = normalized({a[1]*b[2]-a[2]*b[1],
                                                    a[2]*b[0]-a[0]*b[2],
                                                    a[0]*b[1]-a[1]*b[0]});
                    for (const auto& corner : triangle) {
                        mesh->indices.push_back(static_cast<unsigned int>(mesh->vertices.size() / 8));
                        mesh->vertices.insert(mesh->vertices.end(), corner.position.begin(), corner.position.end());
                        const auto& n = corner.hasNormal ? corner.normal : normal;
                        mesh->vertices.insert(mesh->vertices.end(), n.begin(), n.end());
                        mesh->vertices.insert(mesh->vertices.end(), corner.uv.begin(), corner.uv.end());
                    }
                }
            }
        }
        if (file.bad()) throw std::runtime_error("Failed reading mesh file");
        if (mesh->indices.empty()) throw std::runtime_error("Mesh has no faces");
    } catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) + ": " + error.what());
    }
    return mesh;
}
