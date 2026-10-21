# EngineLabs — Scene Camera + Shader Reflection Checkpoint

**Based on Archive(8).zip.** The original Scene, SceneObject, MeshComponent, MeshImporter, and mesh tests are kept. This checkpoint deliberately does **not** implement a Hierarchy; that is the next milestone.

## Build on Debian 13 / Qt6

Install required development packages if needed:

```sh
sudo apt update
sudo apt install build-essential cmake qt6-base-dev libgl1-mesa-dev
```

From the extracted folder:

```sh
./run.sh
```

Or `cmake -S . -B build && cmake --build build -j2 && ./build/EngineLabs`.

Requires an OpenGL **3.3 Core** context. Shader compile/reflection details are logged to the terminal. The Console dock also reports status.

## Scene camera

1. Click the viewport so it has keyboard focus.
2. Hold **right mouse button and drag** to look around.
3. **W/A/S/D** to fly; **Q/E** down/up; **Shift** for faster movement.
4. **Wheel** adjusts flight speed; **F** or **Focus Cube** resets camera toward cube.

## Material reflection (working source pathway)

- Two GLSL programs compiled from Qt resource files: **Lit** and **Unlit** in top toolbar.
- `Shader::load()` compiles stages, links, and reflects active uniforms, name/GL type/location. Simple uniform arrays `[0]` expand into indexed controls.
- `Material::setShader()` preserves values that are compatible with the new shader and creates defaults for newly-discovered uniforms.
- Inspector creates the right widgets for **float, int, bool, vec2, vec3, vec4, mat4, sampler2D** based on actual reflection.
- Extra author-provided editor hints distinguish **color** versus generic vec3/vec4, and limit roughness to a 0..1 numeric control.
- Color picker, per-axis vector editing, matrix numeric grid, texture file picker, checkbox and spin controls update Material CPU values and trigger viewport redraw.
- Renderer uploads matching float/int/bool/vectors/matrices with OpenGL uniform functions, binds texture unit(s) for sampler2D, and draws original cube GPUMesh.
- Camera and directional light uniforms are **engine-managed**, not material controls. The Shader/Renderer boundary keeps Qt widgets away from OpenGL calls.
- Shader and Texture cache in ResourceManager and cleanup inside GL context.

**Limits:** This completes the material-uniform pathway for the listed GLSL 3.3 types, not every possible OpenGL uniform (e.g., uniform blocks, samplerCube, 2D arrays, and structs require future work). No asset database, scene serialization, hierarchy, object selection or PBR engine is included. Shader programs are bundled, so you can run the editor without locating external shader files.

## Verification

- Existing CPU **MeshCreationTests** passed with `g++` in the generation environment.
- **Qt6 GUI build and actual OpenGL display are not verified there** because Qt6 development packages were unavailable. Please run `./run.sh` locally and share any compile error for precise fixes.

## Next milestone (not implemented now)

Hierarchy widget: selection of any SceneObject, generic Component Inspector, adding/removing components. Keep it separate from the current MaterialEditor.




*my notes*
## where ever theres a scene requirement must update with setScene(std::shared_ptr<scene> scene) for switching!
# EldridgeEngineBeta
