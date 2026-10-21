#version 330 core
in vec3 vNormal;
in vec3 vWorldPosition;
in vec2 vUV;
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 Surface;
uniform bool uAffectedByLights;
uniform vec3 uBaseColor;
uniform vec4 uTintColor;
uniform vec2 uUVScale;
uniform float uRoughness;
uniform int uBands;
uniform bool uUseTexture;
uniform sampler2D uAlbedo;
uniform vec3 uLightDirection;
uniform vec3 uCameraPosition;
uniform bool uSceneLights;
struct Light {vec3 ambient,position,diffuse,specular;
    vec3 direction;
    int type;
    float range;
    vec4 shape; // inner/outer cone cosines, rectangle width/height
};
uniform Light lights[32];
uniform int lightCount;

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

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDirection);
    vec3 V = normalize(uCameraPosition - vWorldPosition);
    vec3 H = normalize(L + V);
    float visibility = shadowVisibility(vWorldPosition,N);
    float diffuse = max(dot(N,L), 0.0) * visibility;
    // A bounded number of lighting bands demonstrates reflected int uniforms.
    if (uBands > 1) diffuse = floor(diffuse * float(uBands)) / float(uBands);
    float shine = mix(70.0, 3.0, clamp(uRoughness, 0.0, 1.0));
    float specular = pow(max(dot(N,H), 0.0), shine) * (1.0 - clamp(uRoughness, 0.0, 1.0));
    vec3 sampled = uUseTexture ? texture(uAlbedo, vUV * uUVScale).rgb : vec3(1.0);
    vec3 rgb = uBaseColor * uTintColor.rgb * sampled * (0.20 + diffuse * 0.80);
    rgb += vec3(0.18) * specular * visibility;
    if(uSceneLights)
    {
        rgb=vec3(0);
        for(int i=0;i<min(lightCount,32);++i)
        {
            float attenuation;vec3 direction=sceneLightDirection(lights[i],vWorldPosition,attenuation);
            float brightness=max(dot(N,direction),0.0);
            rgb+=uBaseColor*uTintColor.rgb*sampled*brightness*lights[i].diffuse*attenuation;
        }
    }
    if (!uAffectedByLights)
        rgb = uBaseColor * uTintColor.rgb * sampled;
    Surface = vec4(normalize(vNormal) * 0.5 + 0.5, clamp(uRoughness, 0.04, 1.0));
    FragColor = vec4(rgb, uTintColor.a);
}
