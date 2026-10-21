#include "ProjectFile.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <cmath>
#include <limits>

namespace
{
bool writeJson(const QString& path, const QJsonObject& data, QString& error)
{
    QSaveFile file(path);
    QByteArray bytes = QJsonDocument(data).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    { error = file.errorString(); return false; }
    return true;
}
bool readJson(const QString& path, QJsonObject& data, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    QJsonParseError parse;
    auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
    { error = "Invalid JSON file"; return false; }
    data = document.object();
    return true;
}

// Copy external shader and texture files into a portable asset folder.
// Content hashes avoid overwriting another asset with the same filename.
bool assetPath(QString& path, const QString& document, bool saving, QString& error)
{
    if (path.isEmpty()) return true;
    QDir directory = QFileInfo(document).absoluteDir();
    if (!saving)
    {
        if (QDir::isRelativePath(path)) path = directory.absoluteFilePath(path);
        if (!QFileInfo::exists(path)) { error = "Missing asset: " + path; return false; }
        return true;
    }
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) { error = "Cannot read asset: " + path; return false; }
    QByteArray bytes = input.readAll();
    QString folder = QFileInfo(document).completeBaseName() + "_Assets";
    if (!directory.mkpath(folder)) { error = "Cannot create asset folder"; return false; }
    QString hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    QString relative = folder + "/" + hash.left(20) + "." + QFileInfo(path).suffix();
    QSaveFile output(directory.absoluteFilePath(relative));
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
    { error = output.errorString(); return false; }
    path = relative;
    return true;
}
bool assets(QJsonObject& scene, const QString& document, bool saving, QString& error)
{
    auto objects = scene["objects"].toArray();
    for (int i = 0; i < objects.size(); ++i)
    {
        auto object = objects[i].toObject();
        if (object.value("terrain").isObject())
        {
            auto terrain = object["terrain"].toObject();
            auto layers = terrain["layers"].toArray();
            for (int j = 0; j < layers.size(); ++j)
            {
                auto layer = layers[j].toObject();
                QString path = layer["texture"].toString();
                if (!assetPath(path, document, saving, error)) return false;
                layer["texture"] = path;
                layers[j] = layer;
            }
            terrain["layers"] = layers;
            object["terrain"] = terrain;
            objects[i] = object;
        }
        if (!object["material"].isObject()) continue;
        auto material = object["material"].toObject();
        QString shader = material["shader"].toString();
        if (!assetPath(shader, document, saving, error)) return false;
        material["shader"] = shader;
        auto parameters = material["parameters"].toObject();
        for (const QString& key : parameters.keys())
        {
            auto parameter = parameters[key].toObject();
            if (parameter["type"].toInt(-1) != 7) continue;
            QString texture = parameter["value"].toString();
            if (!assetPath(texture, document, saving, error)) return false;
            parameter["value"] = texture;
            parameters[key] = parameter;
        }
        material["parameters"] = parameters;
        object["material"] = material;
        objects[i] = object;
    }
    scene["objects"] = objects;
    return true;
}
}

bool ProjectFile::save(const QString& path, const SceneManager& manager,
                       const QJsonObject& graphics, QString& error)
{
    QJsonArray scenes;
    int active = 0;
    for (const auto& scene : manager.scenes())
    {
        if (scene.get() == manager.activeScene) active = scenes.size();
        auto data = scene->toJson();
        if (!assets(data, path, true, error)) return false;
        scenes.append(data);
    }
    return writeJson(path, {{"version", 1}, {"type", "project"}, {"scenes", scenes},
                            {"activeScene", active}, {"graphics", graphics}}, error);
}

bool ProjectFile::load(const QString& path, std::vector<std::unique_ptr<Scene>>& result,
                       int& activeScene, QJsonObject& graphics, QString& error)
{
    QJsonObject data;
    if (!readJson(path, data, error)) return false;
    auto entries = data["scenes"].toArray();
    int active = data["activeScene"].toInt(-1);
    if (data["version"].toInt() != 1 || data["type"] != "project" || entries.isEmpty() ||
        active < 0 || active >= entries.size() || !data["graphics"].isObject())
    { error = "Invalid project file"; return false; }
    auto camera = data["graphics"].toObject().value("camera");
    if (!camera.isUndefined())
    {
        if (!camera.isArray() || camera.toArray().size() != 5) { error = "Invalid project camera"; return false; }
        for (const auto& value : camera.toArray())
            if (!value.isDouble() || !std::isfinite(value.toDouble()) || std::abs(value.toDouble()) > std::numeric_limits<float>::max())
            { error = "Invalid project camera"; return false; }
    }
    auto settings=data["graphics"].toObject();
    for(const QString& key:{QString("cameraNear"),QString("cameraFar")})
        if(settings.contains(key) && (!settings[key].isDouble() || !std::isfinite(settings[key].toDouble()) || settings[key].toDouble()<=0 || settings[key].toDouble()>1e8))
        {error="Invalid camera clipping range";return false;}
    if(settings.value("cameraFar").toDouble(500)<=settings.value("cameraNear").toDouble(0.05))
    {error="Camera far plane must exceed the near plane";return false;}
    std::vector<std::unique_ptr<Scene>> loaded;
    for (const auto& entry : entries)
    {
        if (!entry.isObject()) { error = "Invalid project scene"; return false; }
        auto scene = std::make_unique<Scene>();
        auto state = entry.toObject();
        if (!assets(state, path, false, error) || !scene->fromJson(state, error)) return false;
        scene->resetHistory();
        loaded.push_back(std::move(scene));
    }
    result = std::move(loaded);
    activeScene = active;
    graphics = data["graphics"].toObject();
    return true;
}

bool ProjectFile::savePrefab(const QString& path, const Scene& scene, SceneObject& root, QString& error)
{
    const auto& owned = scene.getObjects();
    QJsonArray all = scene.toJson()["objects"].toArray();
    QHash<int, int> indices;
    for (int i = 0; i < int(owned.size()); ++i)
    {
        for (auto* parent = owned[i].get(); parent; parent = parent->parent())
            if (parent == &root) { indices.insert(i, indices.size()); break; }
    }
    if (indices.isEmpty()) { error = "Select an object first"; return false; }
    QJsonArray entries;
    int rootIndex = -1;
    for (int i = 0; i < all.size(); ++i)
    {
        if (!indices.contains(i)) continue;
        auto object = all[i].toObject();
        object["parent"] = indices.value(object["parent"].toInt(), -1);
        if (owned[i].get() == &root) rootIndex = entries.size();
        entries.append(object);
    }
    QJsonObject state {{"version", 1}, {"name", "Prefab"}, {"objects", entries}};
    if (!assets(state, path, true, error)) return false;
    return writeJson(path, {{"version", 1}, {"type", "prefab"}, {"root", rootIndex}, {"scene", state}}, error);
}

bool ProjectFile::loadPrefab(const QString& path, Scene& target, QString& error)
{
    QJsonObject data;
    if (!readJson(path, data, error)) return false;
    if (data["version"].toInt() != 1 || data["type"] != "prefab" || !data["scene"].isObject())
    { error = "Invalid prefab file"; return false; }
    auto state = data["scene"].toObject();
    Scene loaded;
    if (!assets(state, path, false, error) || !loaded.fromJson(state, error)) return false;
    target.appendObjects(loaded);
    target.recordEdit();
    return true;
}
