#pragma once

#include <vector>

struct Mesh
{
    // Eight floats per vertex: position xyz, normal xyz, UV uv.
    std::vector<float> vertices;

    std::vector<unsigned int> indices;

};
