#version 330 core

in vec3 WorldPos;
in vec3 Normal;
in vec2 TexCoords;

out vec4 FragColor;

struct Camera {
    vec3 Pos;
};
uniform Camera camera;

// Uniform names match Material::createPBRMaterial() in the material-system patch.
struct Material {
    vec3 albedo;
    vec3 ambient;
    vec3 specular_color;
    vec3 emissive;
    float metallic;
    float roughness;
    float normalStrength;
    vec3 normal_vector;  // tangent-space normal direction / bias

    sampler2D diffuse;
    sampler2D specular;
    sampler2D normal;
    sampler2D metallic_tex;
    sampler2D roughness_tex;

    int hasDiffuseMap;
    int hasSpecularMap;
    int hasNormalMap;
    int hasMetallicMap;
    int hasRoughnessMap;
};
uniform Material material;

struct Light {
    vec3 ambient;
    vec3 position;
    vec3 diffuse;
    vec3 specular;
};
uniform Light lights[32];
uniform int lightCount;

struct DLight {
    vec3 dir;
    vec3 diffuse;
    vec3 specular;
    vec3 ambient;
    vec3 normal;
};
uniform DLight dlight;

vec3 calculateNormal()
{
    vec3 N = normalize(Normal);
    // Default tangent normal points outward. Normal Direction XYZ adjusts
    // the tangent normal (without changing the mesh geometry).
    vec3 mappedNormal = vec3(0.0, 0.0, 1.0);
    if (material.hasNormalMap != 0) {
        mappedNormal = texture(material.normal, TexCoords).rgb * 2.0 - 1.0;
        // Standard normal-map textures are authored with Y up, while OpenGL
        // tangent space uses Y down. Flip it before the TBN transform.
        mappedNormal.y = -mappedNormal.y;
    }

    // (0,0,1) is neutral. The normal-direction field can tilt that direction.
    mappedNormal += material.normal_vector - vec3(0.0, 0.0, 1.0);
    mappedNormal.xy *= material.normalStrength;
    if (dot(mappedNormal, mappedNormal) < 0.000001)
        return N;
    mappedNormal = normalize(mappedNormal);

    // Build a TBN matrix using screen-space derivatives when possible. Some
    // cube/UV seam cases yield a degenerate derivative basis, so fall back to a
    // stable tangent basis instead of silently discarding the normal map.
    vec3 dp1 = dFdx(WorldPos);
    vec3 dp2 = dFdy(WorldPos);
    vec2 duv1 = dFdx(TexCoords);
    vec2 duv2 = dFdy(TexCoords);
    float det = duv1.x * duv2.y - duv2.x * duv1.y;

    vec3 T = vec3(0.0);
    vec3 B = vec3(0.0);

    if (abs(det) > 0.00001) {
        vec3 tangent = (dp1 * duv2.y - dp2 * duv1.y) / det;
        vec3 bitangent = (dp2 * duv1.x - dp1 * duv2.x) / det;
        vec3 tangentOrtho = tangent - N * dot(N, tangent);

        if (dot(tangentOrtho, tangentOrtho) > 0.000001) {
            T = normalize(tangentOrtho);
            B = cross(N, T);
            B *= (dot(B, bitangent) < 0.0) ? -1.0 : 1.0;
        }
    }

    if (dot(T, T) < 0.000001) {
        vec3 ref = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        T = normalize(cross(ref, N));
        B = cross(N, T);
    }

    return normalize(mat3(T, B, N) * mappedNormal);
}

// This is a roughness-controlled Phong approximation, not a full PBR BRDF.
vec3 lightContribution(
    vec3 L,
    vec3 lightAmbient,
    vec3 lightDiffuse,
    vec3 lightSpecular,
    vec3 N,
    vec3 V,
    vec3 albedo,
    vec3 specColor,
    float metallic,
    float roughness)
{
    float diff = max(dot(N, L), 0.0);
    // Blinn-Phong: a broader, more visible highlight than the earlier sharp reflection.
    vec3 H = normalize(L + V);
    float exponent = mix(64.0, 4.0, roughness);
    float spec = 0.0;
    if (diff > 0.0)
        spec = pow(max(dot(N, H), 0.0), exponent);

    vec3 ambient = lightAmbient * material.ambient * albedo;
    vec3 diffuse = lightDiffuse * diff * albedo * (1.0 - metallic);
    vec3 specular = lightSpecular * spec * specColor;
    return ambient + diffuse + specular;
}

void main()
{
    vec3 N = calculateNormal();
    vec3 V = normalize(camera.Pos - WorldPos);

    // Albedo color tints the texture, or works alone without a texture.
    vec3 albedo = material.albedo;
    if (material.hasDiffuseMap != 0)
        albedo *= texture(material.diffuse, TexCoords).rgb;

    float metallic = clamp(material.metallic, 0.0, 1.0);
    if (material.hasMetallicMap != 0)
        metallic = clamp(metallic * texture(material.metallic_tex, TexCoords).r, 0.0, 1.0);

    float roughness = clamp(material.roughness, 0.04, 1.0);
    if (material.hasRoughnessMap != 0)
        roughness = clamp(roughness * texture(material.roughness_tex, TexCoords).r, 0.04, 1.0);

    // Inspector Specular RGB is now directly adjustable.
    // This is a Phong-style editor preview, not a physically based BRDF.
    vec3 specColor = material.specular_color * mix(vec3(1.0), albedo, metallic);
    if (material.hasSpecularMap != 0)
        specColor *= texture(material.specular, TexCoords).rgb;

    vec3 result = material.emissive;

    for (int i = 0; i < min(lightCount, 32); ++i)
    {
        vec3 L = normalize(lights[i].position - WorldPos);
        result += lightContribution(
            L,
            lights[i].ambient,
            lights[i].diffuse,
            lights[i].specular,
            N, V, albedo, specColor, metallic, roughness
        );
    }

    // dlight.dir points along the rays, so negate it for surface-to-light.
    vec3 L = normalize(-dlight.dir);
    result += lightContribution(
        L,
        dlight.ambient,
        dlight.diffuse,
        dlight.specular,
        N, V, albedo, specColor, metallic, roughness
    );

    FragColor = vec4(result, 1.0);
}
