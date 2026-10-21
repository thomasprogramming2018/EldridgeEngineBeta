#include "modules/renderer/camera.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

int main()
{
    try {
        auto require = [](bool value) {
            if (!value) throw std::runtime_error("Camera check failed");
        };
        Camera camera;
        require(std::abs(camera.forward().length() - 1.f) < 0.0001f);
        require(camera.viewMatrix().map(camera.position).length() < 0.0001f);
        camera.yaw_ = -90.f;
        camera.pitch_ = 0.f;
        require((camera.forward() - QVector3D(0, 0, -1)).length() < 0.0001f);
        camera.pitch_ = 30.f;
        require(std::abs(camera.forward().y() - 0.5f) < 0.0001f);
        camera.position = {10, 5, -2};
        require(camera.viewMatrix().map(camera.position).length() < 0.0001f);
        const auto wide = camera.projectionMatrix(1600, 800);
        require(std::abs(wide(1, 1) / wide(0, 0) - 2.f) < 0.0001f);
        camera.nearPlane=0.5f;camera.farPlane=5000.f;
        const auto clipping=camera.projectionMatrix(800,600);
        require(std::abs(clipping.map(QVector3D(0,0,-0.5f)).z()+1.f)<0.001f);
        require(std::abs(clipping.map(QVector3D(0,0,-5000.f)).z()-1.f)<0.001f);
        const auto minimized = camera.projectionMatrix(0, 0);
        for (int i = 0; i < 16; ++i)
            require(std::isfinite(minimized.constData()[i]));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
