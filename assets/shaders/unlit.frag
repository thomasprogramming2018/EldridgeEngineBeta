#version 330 core
in vec3 vNormal;
in vec3 vWorldPosition;
in vec2 vUV;
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 Surface;
uniform vec3 uBaseColor;
uniform vec4 uTintColor;
uniform vec2 uUVScale;
uniform bool uUseTexture;
uniform sampler2D uAlbedo;
void main() {
    vec3 sampled = uUseTexture ? texture(uAlbedo, vUV * uUVScale).rgb : vec3(1.0);
    Surface = vec4(normalize(vNormal) * 0.5 + 0.5, 1.0);
    FragColor = vec4(uBaseColor * uTintColor.rgb * sampled, uTintColor.a);
}
