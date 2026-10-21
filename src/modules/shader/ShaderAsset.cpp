#include "ShaderAsset.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>

bool ShaderAsset::load(const QString& path)
{
    if (QFileInfo(path).suffix() != "elsha")
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError error;
    auto document = QJsonDocument::fromJson(file.readAll(), &error);

    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;

    auto object = document.object();

    if (object["version"].toInt() != 1 ||
        !object["name"].isString() ||
        !object["vertexSource"].isString() ||
        !object["fragmentSource"].isString())
        return false;

    if (object.contains("rendering") && !object["rendering"].isObject())
        return false;
    auto rendering = object["rendering"].toObject();
    if (rendering.contains("affectedByLights") &&
        !rendering["affectedByLights"].isBool())
        return false;

    name = object["name"].toString();
    affectedByLights = rendering["affectedByLights"].toBool(true);
    vertexSource = object["vertexSource"].toString();
    fragmentSource = object["fragmentSource"].toString();

    return true;
}

bool ShaderAsset::save(const QString& path) const
{
    if (QFileInfo(path).suffix() != "elsha")
        return false;

    QJsonObject object;
    object["version"] = 1;
    object["name"] = name;
    object["vertexSource"] = vertexSource;
    object["fragmentSource"] = fragmentSource;
    QJsonObject rendering;
    rendering["affectedByLights"] = affectedByLights;
    object["rendering"] = rendering;

    QByteArray data = QJsonDocument(object).toJson();

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    if (file.write(data) != data.size())
        return false;

    return file.commit();
}