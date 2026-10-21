# Shader files

Select an object with a material. Its material inspector has a shader picker,
Open .elsha Shader, New Shader, and Edit Shader.

New Shader starts with a working Lit template. Edit the Vertex and Fragment tabs.
Save and Apply writes a real .elsha file, then compiles it. Errors appear in the
Console. A compile error leaves the material's previous shader working. The saved
file still contains your edit, so reopen it to correct the source.

The built-in files are assets/shaders/Lit.elsha, Unlit.elsha, PBR.elsha, and HDRP.elsha.
The picker scans that folder. Open can select .elsha files anywhere else.
The current material's external file also appears in its picker.

## File contents

A .elsha file is JSON containing version, name, vertexSource, fragmentSource,
and rendering settings. Source strings contain the full GLSL program. Let the
editor save the JSON rather than escaping source code yourself.

    "rendering": {
        "affectedByLights": true
    }

ShaderAsset reads and writes the file. Shader compiles it. Material stores values
for user uniforms. Renderer supplies engine uniforms on every frame.

## Lighting choice

The Affected by lights checkbox is saved in the shader file. The renderer sends
its value to this reserved GLSL uniform:

```glsl
uniform bool uAffectedByLights;
```

The fragment shader must use the value. For example:

```glsl
vec3 color = baseColor;
if (uAffectedByLights)
    color = calculateLighting(); // Your shader's lighting function.
FragColor = vec4(color, 1.0);
```

The supplied Lit and PBR templates already implement this choice. A checkbox
cannot rewrite arbitrary GLSL: custom shaders must follow this interface.
There is currently a directional preview light; scene light objects are not
implemented by this change. PBR now uses Cook-Torrance direct lighting. HDRP adds an Eldridge clear-coat
material preset. See ProjectsAndGraphics.md for the rendering limits.

## Uniforms

Vertex attributes use locations 0 = position, 1 = normal, 2 = UV.
Use GLSL 330 core. The renderer supplies:

- uModel, uView, uProjection
- uCameraPosition (and camera.Pos for the current PBR template)
- uTime in seconds
- uAffectedByLights
- uLightDirection for Lit
- lightCount and dlight fields for the PBR preview (point light count is zero)

These values, lights[...] and uLights[...] fields, and uLightCount are reserved
for the engine and hidden from material controls. Supported custom uniforms,
such as `uniform float pulseSpeed;`, appear in the inspector if they are active
in the linked shader. Unused uniforms can be removed by the GLSL compiler.

## Future settings

To add another rendering choice:

1. Add a clearly named field to ShaderAsset.
2. Read and write it inside the JSON rendering object.
3. Add an editor control for it.
4. Apply it in Renderer or pass it to GLSL as a reserved engine uniform.

A setting needs rendering behavior as well as a saved value.

Terrain.elsha supplies four-layer terrain shading. uTerrain* and uShadow* values
plus uLightViewProjection are engine uniforms and hidden from material controls.
The Vulkan tracer evaluates its own PBR model rather than running arbitrary GLSL;
see HardwareRayTracingAndTerrain.md for supported texture/material features.
