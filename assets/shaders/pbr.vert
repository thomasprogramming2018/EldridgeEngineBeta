
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;

out vec3 WorldPos;
out vec3 Normal;
out vec2 TexCoords;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main()
{
    vec4 worldPosition = uModel * vec4(aPos, 1.0);

    WorldPos = worldPosition.xyz;

    Normal = mat3(transpose(inverse(uModel))) * aNormal;

    TexCoords = aTexCoords;

    gl_Position = uProjection * uView * worldPosition;
}
