#version 330 core

in vec3 WorldPos;
in vec3 Normal;
in vec2 TexCoords;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 Surface;

struct Camera {
    vec3 Pos;
};
uniform Camera camera;
uniform bool uAffectedByLights;

// Uniform names match Material::createPBRMaterial() in the material-system patch.
struct Material {
    vec3 albedo;
    vec3 ambient;
    vec3 specular_color;
    vec3 emissive;
    float metallic;
    float roughness;
    float clearCoat;
    float clearCoatRoughness;
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
    vec3 direction;
    int type;
    float range;
    vec4 shape; // inner/outer cone cosines, rectangle width/height

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

// Cook-Torrance lighting: GGX distribution, Smith masking, Schlick Fresnel.
const float PI = 3.14159265;
vec3 lightContribution(vec3 L, vec3 ambient, vec3 diffuse, vec3 specular,
    vec3 N, vec3 V, vec3 albedo, vec3 specColor, float metallic, float roughness)
{
    vec3 H = normalize(L + V);
    float NdotV = max(dot(N, V), 0.001);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float a = roughness * roughness;
    float a2 = a * a;
    float denominator = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float distribution = a2 / max(PI * denominator * denominator, 0.00001);
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float geometryV = NdotV / (NdotV * (1.0 - k) + k);
    float geometryL = NdotL / max(NdotL * (1.0 - k) + k, 0.001);
    vec3 F0 = mix(clamp(specColor, 0.0, 1.0), albedo, metallic);
    vec3 fresnel = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    vec3 reflected = distribution * geometryV * geometryL * fresnel / max(4.0 * NdotV * NdotL, 0.001);
    vec3 diffuseWeight = (1.0 - fresnel) * (1.0 - metallic);
    return ambient * albedo + (diffuseWeight * albedo / PI * diffuse + reflected * specular) * NdotL;
}


uniform bool uShadows;
uniform sampler2D uShadowMap;
uniform mat4 uLightViewProjection;
float shadowVisibility(vec3 position,vec3 normal)
{
    if(!uShadows) return 1.0;
    vec4 clip=uLightViewProjection*vec4(position,1.0);
    vec3 coord=clip.xyz/clip.w*0.5+0.5;
    if(any(lessThan(coord,vec3(0)))||any(greaterThan(coord,vec3(1)))) return 1.0;
    float bias=max(0.00015,0.001*(1.0-dot(normal,normalize(vec3(0.4,1,0.3)))));
    vec2 texel=1.0/vec2(textureSize(uShadowMap,0));
    float visibility=0.0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x)
        visibility+=coord.z-bias<=texture(uShadowMap,coord.xy+vec2(x,y)*texel).r?1.0:0.0;
    return visibility/9.0;
}


// Raster preview samples the center of each source. Vulkan samples its area.
vec3 sceneLightDirection(Light light,vec3 position,out float attenuation)
{
    attenuation=1.0;
    if(light.type==0)return normalize(-light.direction);
    vec3 delta=light.position-position;float distance=length(delta);
    vec3 L=delta/max(distance,.0001);
    float fade=clamp(1.0-pow(distance/max(light.range,.01),4.0),0.0,1.0);
    attenuation=fade*fade/max(distance*distance,.01);
    if(light.type==2)attenuation*=clamp((dot(-L,normalize(light.direction))-light.shape.y)/max(light.shape.x-light.shape.y,.0001),0.0,1.0);
    if(light.type==3)attenuation*=max(dot(-L,normalize(light.direction)),0.0)*light.shape.z*light.shape.w;
    return L;
}

void main()
{
    vec3 N = calculateNormal();
    vec3 V = normalize(camera.Pos - WorldPos);

    // Albedo color tints the texture, or works alone without a texture.
    vec3 albedo = material.albedo;
    if (material.hasDiffuseMap != 0)
        albedo *= pow(texture(material.diffuse, TexCoords).rgb, vec3(2.2));

    float metallic = clamp(material.metallic, 0.0, 1.0);
    if (material.hasMetallicMap != 0)
        metallic = clamp(metallic * texture(material.metallic_tex, TexCoords).r, 0.0, 1.0);

    float roughness = clamp(material.roughness, 0.04, 1.0);
    if (material.hasRoughnessMap != 0)
        roughness = clamp(roughness * texture(material.roughness_tex, TexCoords).r, 0.04, 1.0);

    // Inspector Specular RGB is now directly adjustable.
    // The specular color is the dielectric reflectance used by the GGX BRDF.
    vec3 specColor = material.specular_color;
    if (material.hasSpecularMap != 0)
        specColor *= texture(material.specular, TexCoords).rgb;

    vec3 result = material.emissive;

    for (int i = 0; i < min(lightCount, 32); ++i)
    {
        float attenuation;
        vec3 L=sceneLightDirection(lights[i],WorldPos,attenuation);
        result += lightContribution(
            L,
            lights[i].ambient,
            lights[i].diffuse * attenuation,
            lights[i].specular * attenuation,
            N, V, albedo, specColor, metallic, roughness
        );
    }

    // dlight.dir points along the rays, so negate it for surface-to-light.
    vec3 L = normalize(-dlight.dir);
    float visibility = shadowVisibility(WorldPos,N);
    result += lightContribution(
        L,
        dlight.ambient,
        dlight.diffuse * visibility,
        dlight.specular * visibility,
        N, V, albedo, specColor, metallic, roughness
    );

    if (!uAffectedByLights)
        result = albedo + material.emissive;
    if (uAffectedByLights)
    {
        float coat = clamp(material.clearCoat, 0.0, 1.0);
        vec3 coating = lightContribution(normalize(-dlight.dir), vec3(0.0), vec3(0.0),
            dlight.specular * visibility, N, V, vec3(1.0), vec3(0.04), 0.0,
            clamp(material.clearCoatRoughness, 0.04, 1.0));
        result = result * (1.0 - 0.04 * coat) + coating * coat;
    }
    Surface = vec4(N * 0.5 + 0.5, roughness);
    FragColor = vec4(result, 1.0);
}
