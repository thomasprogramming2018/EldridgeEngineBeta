# Hardware ray tracing and terrain

## Hardware ray tracing

Open Graphics and enable Realtime GPU renderer. See
[LiveViewportPathTracing.md](LiveViewportPathTracing.md) for the full-frame
reference lighting mode, or
[RealtimeHybridLighting.md](RealtimeHybridLighting.md) for the lighting playground,
quality controls, debug views and performance details. The device name and any
capability failure appear below the checkbox. The implementation requires a GPU
with Vulkan 1.2, VK_KHR_acceleration_structure, VK_KHR_ray_query,
VK_KHR_deferred_host_operations and buffer device addresses. CPU Vulkan devices
are rejected. The editor never silently substitutes the CPU tracer for this mode.
The backend was tested on an NVIDIA GeForce RTX 4060.

The viewport offers full-frame reference path tracing, or additive hybrid lighting
over its raster image. Both use GPU filtering and guided upsampling. Moving the
camera updates a persistent Vulkan session instead of rebuilding its scene.
The Path-Traced Reference / Export window retains the complete image renderer and
lets you choose GPU or CPU rendering and export an image.

VulkanRayTracer uploads world-space triangles, builds a bottom-level acceleration
structure and a top-level instance, binds a ray-query compute shader, and dispatches
work on the GPU. The reference shader traces primary, sun-shadow, GI, and reflection rays;
the optional hybrid variant traces a bounded one-bounce lighting contribution.
Its direct and bounce shading uses GGX. Base-color, normal, metallic and roughness
maps, emission, and an additional direct clear-coat lobe are supported.
Built-in PBR/HDRP/terrain albedo textures are decoded from sRGB before lighting;
normal, roughness and metallic maps remain linear data. Normal
strength is respected. Terrain layers are baked into a color map for the tracer.
Texture arrays currently resample maps to 256 by 256; the backend limits a render
to 256 material records. Width is limited to 1920 and height to 1080.

The Vulkan guide explains why both BLAS/TLAS and ray-query support are required:
https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html

## Build

Qt 6, C++17, and OpenGL are still required for the editor. Install Vulkan development
headers and the Vulkan loader to enable the hardware backend at build time. CMake
builds the rest of the editor without Vulkan when those dependencies are absent.
Drivers must expose the features above at runtime.

The original source is assets/shaders/hardware.comp. A compiled hardware.comp.spv
is included for builds without a shader compiler. If glslangValidator is installed,
CMake recompiles it when the source changes. To rebuild manually:

    glslangValidator --target-env vulkan1.2 -V assets/shaders/hardware.comp -o assets/shaders/hardware.comp.spv

## Terrain workflow

1. Click Create Terrain, or choose Terrain in Create Object. The editor selects
   and frames the terrain and assigns Terrain.elsha. Camera clipping adjusts to
   the framed size and is saved with the project.
2. In the Terrain inspector, set world size and resolution. Applying these resets
   the samples after confirmation. Supported resolutions are 33, 65, 129, 257 and
   513 samples per side, including both edges.
3. Generate seeded five-octave hills, import a heightmap, or sculpt a flat grid.
   Heightmap imports retain 16-bit grayscale precision and resample to the grid.
   Height = offset + normalized image value times height range.
4. Set four layer textures and tints. Missing textures use a white texture, so the
   tint works by itself. Layer tiling controls texture repeats.
5. Enable Edit terrain in viewport. Left-drag to raise, lower, smooth, flatten,
   or paint a selected texture layer. Right-drag still controls the camera. A
   yellow ring shows the brush. Radius and flatten height use terrain-local units.
   One drag is one undo step. Selecting a different object disables the brush.
6. Apply thermal erosion to relax steep slopes. This operation conserves total
   sample height and is undoable.
7. Export 16-bit heightmaps or RGBA weight maps. Set the export offset/range to
   include the minimum and maximum heights; exporting outside this range reports
   an error rather than silently clipping. Importing a weight map normalizes all
   four channels to a total of 255.

TerrainComponent owns heights, four layers and the weight map. It generates a
full-resolution CPU mesh for ray tracing and collision queries, and 32-cell chunks
with four LODs for raster rendering. Skirts cover borders between LODs. Normals
come from full-resolution height derivatives, so adjacent chunks agree at seams.
The Terrain shader writes normal/roughness for the existing screen-space effects.

heightAt samples the surface in local coordinates. raycast intersects a world ray
against the transformed heightfield. A physics integration can use these queries;
this project does not yet contain a rigid-body physics engine.

Scenes save heights, weight maps, settings and layer paths, then regenerate their
meshes on load. Projects and prefabs package terrain layer texture files. Prefabs
preserve the terrain's local transform and children. Terrain edits are included
in the existing undo/redo system.

## Additional raster quality

A 2048-square directional shadow map with PCF filtering supplies raster shadows
for the built-in Lit, PBR, HDRP and Terrain shaders. HDR bloom spreads bright scene
emission before tone mapping. Both have Graphics toggles and saved settings.

## Practical limits

This is an initial hardware path-tracing backend integrated with an OpenGL editor,
not a complete Unreal renderer. Vulkan lighting buffers are copied through CPU
memory for OpenGL
composition. Acceleration structures are rebuilt for a new scene snapshot,
while camera movement reuses the existing acceleration structures and pipeline.
There is no GPU-only Vulkan/OpenGL image interop or incremental BLAS refitting yet.
These costs matter for large scenes and rapid terrain editing. The realtime path
uses a small GPU temporal/spatial filter, and the reference
preview uses a CPU spatial filter. Neither is a trained denoiser.

The hardware path evaluates its own PBR materials, not arbitrary user GLSL. The reference image does not include raster screen-space fog, god rays, AO or SSR.
The realtime hybrid composition retains those raster effects.
Ray-traced GI/shadows/reflections operate on all uploaded geometry, including
geometry outside the current screen. This version does not implement motion blur,
skinned meshes, alpha-cutout geometry, transmissive materials or spectral transport.

The terrain is a bounded heightfield: it does not support overhangs/caves, holes,
world streaming, vegetation, water simulation or Unreal's Landscape tools. A single
shadow map covers scene bounds; very large worlds would need cascaded/virtual
shadows. Imported images remain bounded by the grid resolution. These limits are
explicit rather than represented as completed engine-parity features.
