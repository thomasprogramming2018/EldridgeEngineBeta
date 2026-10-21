#pragma once
#include <QJsonObject>
#include <algorithm>

struct RenderSettings
{
    bool ambientOcclusion = true;
    bool shadows = true;
    bool bloom = true;
    bool screenSpaceReflections = false;
    bool godRays = false;
    bool volumetricFog = false;
    bool hardwareRayTracing = false;
    bool rayFullFrame = true;
    int raySamples = 128;
    int rayBounces = 4;
    int rayWidth = 640;
    bool rayDenoise = true;
    int rayDenoisePasses = 3, rayMovingSamples = 2;
    float rayDenoiseStrength = 1.f, rayIndirectClamp = 10.f;
    bool rayGI = true;
    float raySunIntensity = 3.f, raySkyIntensity = 1.f, raySunAngle = 0.53f;
    float rayBudgetMs = 12.f;
    float rayStrength = 1.f;
    int lightingDebug = 0; // Final, ray lighting, normals, temporal history.
    float exposure = 1.f;
    float fogDensity = 0.035f;

    QJsonObject toJson() const
    {
        return {{"shadows",shadows},{"bloom",bloom},{"ao", ambientOcclusion}, {"ssr", screenSpaceReflections},
                {"godRays", godRays}, {"fog", volumetricFog},
                {"exposure", exposure}, {"fogDensity", fogDensity},
                {"hardwareRayTracing",hardwareRayTracing},{"raySamples",raySamples},{"rayBounces",rayBounces},
                {"rayFullFrame",rayFullFrame},
                {"rayWidth",rayWidth},{"rayDenoise",rayDenoise},
                {"rayDenoisePasses",rayDenoisePasses},{"rayMovingSamples",rayMovingSamples},
                {"rayDenoiseStrength",rayDenoiseStrength},{"rayIndirectClamp",rayIndirectClamp},
                {"rayGI",rayGI},{"raySunIntensity",raySunIntensity},{"raySkyIntensity",raySkyIntensity},{"raySunAngle",raySunAngle},
                {"rayBudgetMs",rayBudgetMs},{"rayStrength",rayStrength},{"lightingDebug",lightingDebug}};
    }
    void fromJson(const QJsonObject& data)
    {
        shadows=data["shadows"].toBool(true);
        bloom=data["bloom"].toBool(true);
        ambientOcclusion = data["ao"].toBool(true);
        screenSpaceReflections = data["ssr"].toBool(false);
        godRays = data["godRays"].toBool(false);
        volumetricFog = data["fog"].toBool(false);
        hardwareRayTracing=data["hardwareRayTracing"].toBool(false);
        rayFullFrame=data["rayFullFrame"].toBool(true);
        raySamples=std::clamp(data["raySamples"].toInt(128),1,512);
        rayBounces=std::clamp(data["rayBounces"].toInt(4),1,8);
        rayWidth=std::clamp(data["rayWidth"].toInt(640),160,1920);
        rayDenoise=data["rayDenoise"].toBool(true);
        rayDenoisePasses=std::clamp(data["rayDenoisePasses"].toInt(3),1,4);
        rayMovingSamples=std::clamp(data["rayMovingSamples"].toInt(2),1,4);
        rayDenoiseStrength=std::clamp(float(data["rayDenoiseStrength"].toDouble(1)),0.1f,2.f);
        rayIndirectClamp=std::clamp(float(data["rayIndirectClamp"].toDouble(10)),0.f,100.f);
        rayGI=data["rayGI"].toBool(true);
        raySunIntensity=std::clamp(float(data["raySunIntensity"].toDouble(3)),0.f,100.f);
        raySkyIntensity=std::clamp(float(data["raySkyIntensity"].toDouble(1)),0.f,20.f);
        raySunAngle=std::clamp(float(data["raySunAngle"].toDouble(0.53)),0.f,20.f);
        rayBudgetMs=std::clamp(float(data["rayBudgetMs"].toDouble(12)),4.f,40.f);
        rayStrength=std::clamp(float(data["rayStrength"].toDouble(1)),0.f,4.f);
        lightingDebug=std::clamp(data["lightingDebug"].toInt(0),0,3);
        exposure = std::clamp(float(data["exposure"].toDouble(1)), 0.1f, 8.f);
        fogDensity = std::clamp(float(data["fogDensity"].toDouble(0.035)), 0.f, 0.3f);
    }
};
