#version 330 core

in vec2 vUV;

out vec4 FragColor;

uniform sampler2D uAlbedo;

uniform vec3 uBaseColor;

uniform float uRoughness;

void main()
{
    vec3 albedo = texture(uAlbedo, vUV).rgb;

    float brightness = 1.0 - 0.25 * clamp(uRoughness, 0.0, 1.0);

    FragColor = vec4(albedo * uBaseColor * brightness, 1.0);
}
