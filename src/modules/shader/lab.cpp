#include "Shader.h"
#include <QApplication>
#include <QDebug>
#include <QOpenGLWidget>
#include <QSurfaceFormat>

// The lab owns its context. Do not call OpenGL before initializeGL().
class ShaderLabWidget final : public QOpenGLWidget,
                              protected QOpenGLFunctions_3_3_Core
{
public:
    ~ShaderLabWidget() override
    {
        if (context()) {
            makeCurrent();
            shader_.release();
            doneCurrent();
        }
    }

protected:
    void initializeGL() override
    {
        if (!initializeOpenGLFunctions()) {
            qWarning() << "OpenGL 3.3 Core is unavailable";
            return;
        }

        const QString folder = QString::fromUtf8(ELDRIDGE_SHADER_SOURCE_DIR);
        ShaderDescription description{{
            {QOpenGLShader::Vertex, folder + "/reflection.vert"},
            {QOpenGLShader::Fragment, folder + "/reflection.frag"}
        }};
        if (!shader_.load(description)) return;

        shader_.discoverUniforms(*this);
        qDebug() << "Editor definitions (vec3 only):"
                 << shader_.parameterDefinitions.size();
    }

    void paintGL() override
    {
        glClearColor(0.10f, 0.12f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // Intentionally no draw call: today's lab is uniform introspection.
    }

private:
    Shader shader_;
};

int runLab(int argc, char* argv[])
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    ShaderLabWidget lab;
    lab.setWindowTitle("EldridgeEngine | Shader Reflection Lab");
    lab.resize(500, 320);
    lab.show();
    return app.exec();
}
