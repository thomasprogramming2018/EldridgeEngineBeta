#pragma once
#include <QString>
#include <QtGui/qopengl.h>

// CPU-side editor metadata: GLSL knows a vec3, but not whether it's a color.

enum class EditorHint
{
    Auto,
    Color,
    Slider
};
struct ShaderParameter
{
    QString name;
    EditorHint editor;
    GLenum glType = 0;
    GLint location = -1;

    bool editable = true;
};
