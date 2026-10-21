# EngineLabs architecture map

```text
main.cpp (composition root)
  SceneManager -> Scene -> SceneObject -> MeshComponent (CPU Mesh)
  QMainWindow
     central: Renderer : QOpenGLWidget
         ResourceManager [shared Shader cache, GPU Texture cache]
             Shader (linked program, reflected uniforms, optional UI hints)
         Material (shared Shader reference; independent CPU values)
         GPUMesh (VAO, VBO, EBO) -> glDrawElements
         Camera position/yaw/pitch -> uView, uProjection
     right: MaterialEditor (Qt controls read Shader.uniforms)
     left: Scene Tools help (NO hierarchy yet)
     bottom: Console status
```

## Read in this order

1. `src/main.cpp` — Qt docks, top toolbar, and signal wiring.
2. `src/modules/renderer/Renderer.cpp` — context initialization, WASD camera, paintGL.
3. `src/modules/shader/Shader.cpp` — compile, link, reflect once.
4. `src/modules/material/Material.cpp` — preserve data across shader switches.
5. `src/modules/editor/MaterialEditor.cpp` — widgets from reflected types.
6. `src/modules/resources/ResourceManager.cpp` — shader and texture caching.
7. `src/modules/renderer/GPUMesh.cpp` — upload + draw (existing Mesh data).

## Data flow when editing a material

`MaterialEditor spin/color/file picker` → `Material::set` → `materialChanged()` → `Renderer::update()` → `paintGL()` → `Shader::bind()` → `glUniform*` → `GPUMesh::draw()`.

`MaterialEditor` never makes OpenGL calls. `ResourceManager` only creates textures while GL is current in `paintGL()`. Shader reloads replace the program only after link success. GLSL reflection is cached; it does not rerun per frame.

## Learn without restarting

**First tiny lab:** Change the roughness number and observe the highlight. Find `uRoughness` in `lit.frag`, then follow the Renderer `glUniform1f` upload. Explain these three lines to yourself before proceeding.

Second lab (when ready): temporarily remove a custom GLSL material uniform, rebuild, and notice how the Inspector loses that control because reflection no longer reports it.

Third lab: add a new `uniform float uMyValue;` that actually changes fragment color. The Inspector should automatically create a numeric field without adding a hardcoded C++ `uMyValue` control.
