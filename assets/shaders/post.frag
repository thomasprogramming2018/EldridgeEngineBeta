#version 330 core
in vec2 uv;
out vec4 FragColor;
uniform sampler2D sceneColor;
uniform sampler2D surfaceData;
uniform sampler2D sceneDepth;
uniform mat4 view, projection, inverseProjection, inverseView;
uniform bool useAO, useSSR, useRays, useFog, useBloom;
uniform float exposure, fogDensity;
uniform sampler2D rayLighting, raySurface;
uniform mat4 rayViewProjection;
uniform vec3 rayEye;
uniform bool useHybrid, rayDenoise, fullRayFrame;
uniform float rayStrength;
uniform int lightingDebug;

// Depth/normal guided upsampling: low resolution lighting, full resolution edges.
vec4 gatherLighting(vec3 world, vec3 normal)
{
    vec4 clip = rayViewProjection * vec4(world, 1);
    vec2 coord = clip.xy / max(clip.w, 0.00001) * 0.5 + 0.5;
    if (clip.w <= 0.0 || any(lessThan(coord, vec2(0))) || any(greaterThan(coord, vec2(1)))) return vec4(0);
    vec2 texel = 1.0 / vec2(textureSize(rayLighting, 0));
    float expectedDepth = length(world - rayEye);
    vec4 lighting = vec4(0); float total = 0.0;
    int radius = rayDenoise ? (fullRayFrame ? 1 : 2) : 0;
    for (int y = -radius; y <= radius; ++y) for (int x = -radius; x <= radius; ++x)
    {
        vec2 sampleUV = coord + vec2(x, y) * texel;
        if (any(lessThan(sampleUV, vec2(0))) || any(greaterThan(sampleUV, vec2(1)))) continue;
        vec4 surface = texture(raySurface, sampleUV);
        if (surface.w <= 0.0) continue;
        float normalWeight = pow(max(dot(surface.xyz, normal), 0.0), fullRayFrame ? 16.0 : 24.0);
        float depthWeight = exp(-abs(surface.w - expectedDepth) / max(0.03, expectedDepth * 0.015));
        float weight = normalWeight * depthWeight * exp(-float(x*x + y*y) * 0.3);
        lighting += texture(rayLighting, sampleUV) * weight;
        total += weight;
    }
    return total > 0.05 ? lighting / total : vec4(0);
}
vec4 gatherSky(vec3 direction)
{
    vec4 clip = rayViewProjection * vec4(direction, 0);
    vec2 coord = clip.xy / max(clip.w, 0.00001) * 0.5 + 0.5;
    if (clip.w <= 0.0 || any(lessThan(coord, vec2(0))) || any(greaterThan(coord, vec2(1)))) return vec4(0);
    // Never blur a foreground object into the sky.
    vec2 texel = 1.0 / vec2(textureSize(rayLighting, 0));
    vec4 color = vec4(0); float count = 0.0;
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x)
    {
        vec2 sampleUV = coord + vec2(x, y) * texel;
        if (texture(raySurface, sampleUV).w != 0.0) continue;
        color += texture(rayLighting, sampleUV); count += 1.0;
    }
    return count > 0.0 ? color / count : vec4(0);
}
vec3 positionAt(vec2 coord)
{
    float depth = texture(sceneDepth, coord).r;
    vec4 point = inverseProjection * vec4(coord * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return point.xyz / point.w;
}
bool inside(vec2 coord) { return all(greaterThan(coord, vec2(0))) && all(lessThan(coord, vec2(1))); }
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898,78.233))) * 43758.5453); }
void main()
{
    vec3 color = texture(sceneColor, uv).rgb;
    float depth = texture(sceneDepth, uv).r;
    vec3 position = positionAt(uv);
    vec4 surface = texture(surfaceData, uv);
    vec3 normal = normalize(cross(dFdx(position), dFdy(position)));
    if (dot(normal, -position) < 0.0) normal = -normal;
    if (length(surface.xyz) > 0.1)
        normal = normalize(mat3(view) * (surface.xyz * 2.0 - 1.0));

    vec4 indirect = vec4(0);
    if (useHybrid && depth < 0.99999 && length(surface.xyz) > 0.1)
    {
        vec3 world = (inverseView * vec4(position, 1)).xyz;
        indirect = gatherLighting(world, normalize(surface.xyz * 2.0 - 1.0));
        if (fullRayFrame && indirect.a > 0.0) color = indirect.rgb;
        else if (!fullRayFrame) color += indirect.rgb * rayStrength;
    }
    if (fullRayFrame && depth >= 0.99999)
    {
        vec3 direction = normalize(mat3(inverseView) * position);
        indirect = gatherSky(direction);
        if (indirect.a > 0.0) color = indirect.rgb;
    }
    if (lightingDebug != 0)
    {
        vec3 debugColor = vec3(0);
        if (lightingDebug == 1) debugColor = indirect.rgb / (indirect.rgb + vec3(1));
        if (lightingDebug == 2 && depth < 0.99999) debugColor = normal * 0.5 + 0.5;
        if (lightingDebug == 3) debugColor = mix(vec3(0.9, 0.05, 0.03), vec3(0.05, 0.9, 0.4), clamp(indirect.a / 32.0, 0.0, 1.0));
        FragColor = vec4(debugColor, 1);
        return;
    }

    // Hemisphere depth sampling: nearby geometry occludes ambient lighting.
    if (useAO && !fullRayFrame && depth < 0.99999)
    {
        float occlusion = 0.0;
        float radius = 0.6;
        float angle = hash(uv) * 6.283185;
        for (int i = 0; i < 12; ++i)
        {
            float a = angle + float(i) * 2.39996;
            vec3 direction = normalize(vec3(cos(a), sin(a), float(i + 1) / 12.0));
            vec3 tangent = normalize(abs(normal.z) < 0.99 ? cross(normal, vec3(0,0,1)) : cross(normal, vec3(0,1,0)));
            vec3 samplePosition = position + mat3(tangent, cross(normal, tangent), normal) * direction * radius;
            vec4 projected = projection * vec4(samplePosition, 1);
            vec2 coord = projected.xy / projected.w * 0.5 + 0.5;
            if (!inside(coord)) continue;
            float sampleZ = positionAt(coord).z;
            float range = smoothstep(0.0, 1.0, radius / max(abs(position.z - sampleZ), 0.001));
            if (sampleZ >= samplePosition.z + 0.025) occlusion += range;
        }
        color *= 1.0 - 0.65 * occlusion / 12.0;
    }

    // Screen-space ray marching. Only currently visible geometry can reflect.
    if (useSSR && !fullRayFrame && depth < 0.99999 && surface.a < 0.9)
    {
        vec3 direction = reflect(normalize(position), normal);
        vec3 origin = position + normal * 0.05;
        for (int i = 1; i <= 48; ++i)
        {
            vec3 samplePosition = origin + direction * float(i) * 0.15;
            if (samplePosition.z >= -0.05) break;
            vec4 projected = projection * vec4(samplePosition, 1);
            vec2 coord = projected.xy / projected.w * 0.5 + 0.5;
            if (!inside(coord)) break;
            float gap = positionAt(coord).z - samplePosition.z;
            if (gap > 0.0 && gap < 0.22 && texture(sceneDepth, coord).r < 0.99999)
            {
                float edge = smoothstep(0.0, 0.12, min(min(coord.x, coord.y), min(1.0-coord.x, 1.0-coord.y)));
                float fresnel = 0.08 + 0.8 * pow(1.0 - max(dot(normal, normalize(-position)),0.0), 5.0);
                color = mix(color, texture(sceneColor, coord).rgb, edge * fresnel * (1.0 - surface.a) * (indirect.a > 0.0 ? 0.0 : 1.0));
                break;
            }
        }
    }

    vec3 sun = normalize(vec3(0.4, 1.0, 0.3));
    vec4 sunClip = projection * view * vec4(sun * 1000.0, 0.0);
    vec2 sunUV = sunClip.xy / max(abs(sunClip.w), 0.001) * 0.5 + 0.5;
    if (useRays && sunClip.w > 0.0)
    {
        vec2 stepUV = (sunUV - uv) / 40.0;
        vec2 coord = uv;
        float scattering = 0.0;
        float decay = 1.0;
        for (int i = 0; i < 40; ++i)
        {
            coord += stepUV;
            if (inside(coord) && texture(sceneDepth, coord).r > 0.99999)
                scattering += decay * exp(-length(coord - sunUV) * 10.0);
            decay *= 0.96;
        }
        color += vec3(1.0, 0.86, 0.62) * scattering * 0.025;
    }
    if (useFog)
    {
        vec3 eye = inverseView[3].xyz;
        vec3 worldEnd = (inverseView * vec4(position, 1)).xyz;
        vec3 direction = normalize(worldEnd - eye);
        float distanceToSurface = min(length(worldEnd - eye), 100.0);
        float stepSize = distanceToSurface / 24.0;
        float transmittance = 1.0;
        vec3 scattering = vec3(0.0);
        for (int i = 0; i < 24; ++i)
        {
            vec3 samplePosition = eye + direction * (float(i) + 0.5) * stepSize;
            float density = fogDensity * exp(-max(samplePosition.y, 0.0) * 0.22);
            float lightPhase = 0.25 + pow(max(dot(direction, sun), 0.0), 8.0);
            float absorption = exp(-density * stepSize);
            scattering += transmittance * (1.0 - absorption) * vec3(0.55, 0.65, 0.8) * lightPhase;
            transmittance *= absorption;
        }
        color = color * transmittance + scattering;
    }
    if (useBloom)
    {
        vec2 texel = 1.0 / vec2(fullRayFrame ? textureSize(rayLighting, 0) : textureSize(sceneColor, 0));
        vec3 glow = vec3(0);
        for (int radius = 1; radius <= 3; ++radius)
            for (int direction = 0; direction < 8; ++direction)
            {
                float angle = float(direction) * 0.785398;
                vec2 coord = uv + vec2(cos(angle),sin(angle)) * texel * float(radius * radius * 2);
                vec3 sampleColor = fullRayFrame ? texture(rayLighting, coord).rgb : texture(sceneColor, coord).rgb;
                glow += max(sampleColor - vec3(1.0), vec3(0.0));
            }
        color += glow * (0.12 / 24.0);
    }
    // Filmic HDR tone mapping followed by display gamma.
    color *= exposure;
    color = clamp((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
    FragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
