# Projects, prefabs and graphics

## Editor controls

- Create Object on the toolbar, or the hierarchy background menu, opens object creation.
- Choose Cube, OBJ Mesh, or Empty. For meshes, choose PBR, HDRP, Lit, or Unlit.
- OBJ import loads geometry and UVs. Assign a shader and textures in the material inspector; OBJ material libraries are not imported.
- File > Save Project (Ctrl+Alt+S) writes an .elproject and its asset folder.
- File > Open Project (Ctrl+Alt+O) restores all scene tabs, the active scene, graphics settings, and the active camera.
- File > Save Selected Prefab writes the selected object and all its children to .elprefab.
- File > Load Prefab creates independent copies in the active scene. Undo removes that creation.
- Graphics contains AO, SSR, god rays, volumetric height fog, exposure, fog density, and Path-Traced Reference / Export.

## Project files

An .elproject contains the scene snapshots. Shader and texture dependencies are
copied into a sibling folder named ProjectName_Assets. Content hashes keep assets
with the same filename separate. Move the project file and that folder together.
Geometry is embedded, so the original OBJ file is not required when reopening.
Prefabs use the same packaging scheme and include local transforms and parenting.
The prefab root keeps its local transform when detached from an external parent.
Instances are copies; editing a prefab file does not update existing instances.

Project loading validates every scene and asset before replacing the open
project. Unsaved edits can be saved, discarded, or kept by canceling the open.
Scene-only .elscene files retain their existing external-asset behavior. Save a
project when you want dependencies copied into a portable folder.

## Material presets

PBR uses Cook-Torrance direct lighting with GGX, Smith masking, and Schlick Fresnel.
HDRP is an Eldridge high-quality material preset with an additional clear-coat
lobe. It is not Unity's HDRP pipeline. Both write linear HDR color and surface
normal/roughness data for post-processing. The final pass applies filmic tone
mapping and display gamma. There is currently a directional preview light.
This implementation does not promise parity with Unreal Engine.

Custom shader authors can write these outputs to participate in SSR:

```glsl
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 Surface;
// After evaluating your material:
Surface = vec4(normalize(worldNormal) * 0.5 + 0.5, roughness);
```

Without Surface, depth-derived normals are used for AO and the surface is treated
as rough for SSR. Material uniforms are still reflected automatically.

## Real-time effects and their limits

- Ambient occlusion samples nearby depth in a hemisphere. This is screen-space AO.
- SSR marches a reflected ray through the visible depth buffer. Off-screen objects and hidden geometry cannot reflect through this method. Rough surfaces receive less reflection.
- God rays integrate a visible directional sun through a depth-based occlusion mask. Look toward the sun to see them; it need not be visible from every camera direction.
- Volumetric height fog integrates density and directional scattering in 24 steps along the camera ray. It is a single-scattering preview without volumetric shadow maps or multiple scattering.
- HDR tone mapping and exposure operate on a floating-point scene buffer.

The graphics tests compare actual rendered pixels with each effect enabled and
disabled. These are practical OpenGL 3.3 effects with the limitations above.

## Ray tracing and terrain

Graphics offers live reference path tracing or hybrid Vulkan lighting, with GPU
filtering, adaptive resolution, debug views and an editable Lighting Playground.
Path-Traced Reference / Export offers full-image GPU or CPU rendering. The GPU backend requires
a Vulkan ray-query capable GPU, traces GI/shadows/reflections, and supports normal,
metallic, roughness and base-color maps. Unsupported devices are reported explicitly.
The original CPU preview remains available with fewer material features.

Create Terrain adds an editable heightfield with import/export, procedural hills,
thermal erosion, four painted texture layers, chunk LOD, and world ray queries.
Terrain is included in scenes, projects, prefabs and undo/redo.

See [LiveViewportPathTracing.md](LiveViewportPathTracing.md) to use reference lighting
in the viewport, or [RealtimeHybridLighting.md](RealtimeHybridLighting.md) for hybrid lighting.
See HardwareRayTracingAndTerrain.md for build requirements, terrain algorithms,
and limitations. Directional shadow maps and HDR bloom are also available in the
raster Graphics controls.

## Loading image

Startup and project loading use the loading screen. Replace
assets/loading-placeholder.svg, or add assets/loading-placeholder.png, to use
your own artwork. PNG takes priority. A text background is used if neither image
can be loaded. Loading messages report the current stage; they are not fabricated
percentage estimates.

## Code map

ProjectFile reads, validates, and packages documents. ObjectCreationDialog only
collects creation choices. Renderer uploads one GPU mesh per mesh and draws the
scene into PostProcessor's HDR targets. post.frag implements the screen-space
passes. SoftwareRayTracer contains the CPU ray/triangle and BVH code;
RayTraceDialog owns its worker thread and its cancellation flag.
