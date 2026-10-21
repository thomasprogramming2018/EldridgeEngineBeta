#pragma once
class Scene;
class Camera;

// A separate editable scene: shiny spheres, emissive panels and shadow-casting beams.
namespace LightingDemo
{
    void populate(Scene& scene);
    void frame(Camera& camera);
}
