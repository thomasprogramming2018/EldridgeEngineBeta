#version 330 core
in vec2 uv;
layout(location=0) out vec4 Lighting;
layout(location=1) out vec4 Surface;
uniform sampler2D currentLighting, currentSurface, oldLighting, oldSurface;
uniform mat4 inverseViewProjection, oldViewProjection;
uniform vec3 eye, oldEye;
uniform bool historyValid, fullFrame, cumulative;
uniform float historyLimit;
void main()
{
    // Vulkan readback starts with the top row; framebuffer textures start at the bottom.
    vec2 source = vec2(uv.x, 1.0 - uv.y);
    Surface = texture(currentSurface, source);
    Lighting = vec4(0);
    bool sky = fullFrame && Surface.w == 0.0;
    if (Surface.w <= 0.0 && !sky) return;
    vec4 incoming = texture(currentLighting, source);
    float samples = max(incoming.a, 1.0);
    vec3 color = incoming.rgb / samples;
    vec4 farPoint = inverseViewProjection * vec4(uv * 2.0 - 1.0, 1, 1);
    vec3 direction = normalize(farPoint.xyz / farPoint.w - eye);
    vec3 world = eye + direction * Surface.w;
    // The sky rotates with the camera but has no parallax from translation.
    vec4 previousClip = sky ? oldViewProjection * vec4(direction, 0) : oldViewProjection * vec4(world, 1);
    vec2 previous = previousClip.xy / max(previousClip.w, 0.00001) * 0.5 + 0.5;
    float count = 0.0;
    // Stationary full-frame buffers already contain every sample on the Vulkan GPU.
    // Displaying a later buffer must not count earlier samples a second time.
    if (!cumulative && historyValid && previousClip.w > 0.0 &&
        all(greaterThan(previous, vec2(0))) && all(lessThan(previous, vec2(1))))
    {
        vec4 old = texture(oldSurface, previous);
        float expectedDepth = length(world - oldEye);
        bool sameSurface = sky ? old.w == 0.0 :
            old.w > 0.0 && dot(old.xyz, Surface.xyz) > 0.92 &&
            abs(old.w - expectedDepth) < max(0.03, expectedDepth * 0.008);
        if (sameSurface)
        {
            vec4 history = texture(oldLighting, previous);
            count = min(history.a, max(0.0, historyLimit - samples));
            color = mix(color, history.rgb, count / (count + samples));
        }
    }
    Lighting = vec4(color, count + samples);
}
