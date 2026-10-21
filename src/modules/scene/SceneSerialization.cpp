#include "Scene.h"
#include "../componentsBackend/LightComponent/LightComponent.h"
#include "../componentsBackend/TransformComponent/TransformComponent.h"
#include "../componentsBackend/MeshComponent/MeshComponent.h"
#include "../material/MaterialComponent.h"
#include "../componentsBackend/TerrainComponent/TerrainComponent.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <cmath>
#include <climits>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace
{
void check(bool valid)
{
    if (!valid) throw std::runtime_error("Invalid scene data");
}

double number(const QJsonValue& value)
{
    check(value.isDouble() && std::isfinite(value.toDouble()));
    check(std::abs(value.toDouble()) <= std::numeric_limits<float>::max());
    return value.toDouble();
}

int integer(const QJsonValue& value)
{
    double result = number(value);
    check(std::floor(result) == result && result >= INT_MIN && result <= INT_MAX);
    return int(result);
}

QJsonArray vectorJson(const QVector3D& value)
{
    return {value.x(), value.y(), value.z()};
}

QVector3D readVector(const QJsonValue& value)
{
    check(value.isArray() && value.toArray().size() == 3);
    auto a = value.toArray();
    return {float(number(a[0])), float(number(a[1])), float(number(a[2]))};
}

QJsonObject parameterJson(const MaterialValue& parameter)
{
    QJsonObject result;
    result["type"] = int(parameter.index());
    std::visit([&](const auto& value)
    {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, float> || std::is_same_v<T, int> || std::is_same_v<T, bool>)
            result["value"] = value;
        else if constexpr (std::is_same_v<T, TexturePath>)
            result["value"] = value.path;
        else
        {
            QJsonArray values;
            if constexpr (std::is_same_v<T, QMatrix4x4>)
            {
                for (int row = 0; row < 4; ++row)
                    for (int column = 0; column < 4; ++column)
                        values.append(value(row, column));
            }
            else
            {
                int count = 3;
                if constexpr (std::is_same_v<T, QVector2D>) count = 2;
                if constexpr (std::is_same_v<T, QVector4D>) count = 4;
                for (int i = 0; i < count; ++i) values.append(value[i]);
            }
            result["value"] = values;
        }
    }, parameter);
    return result;
}

MaterialValue readParameter(const QJsonObject& data)
{
    int type = integer(data["type"]);
    auto value = data["value"];
    if (type == 0) return float(number(value));
    if (type == 1) return integer(value);
    if (type == 2) { check(value.isBool()); return value.toBool(); }
    if (type == 7) { check(value.isString()); return TexturePath{value.toString()}; }
    check(value.isArray());
    auto a = value.toArray();
    if (type == 3)
    {
        check(a.size() == 2);
        return QVector2D(number(a[0]), number(a[1]));
    }
    if (type == 4) return readVector(value);
    if (type == 5)
    {
        check(a.size() == 4);
        return QVector4D(number(a[0]), number(a[1]), number(a[2]), number(a[3]));
    }
    check(type == 6 && a.size() == 16);
    QMatrix4x4 matrix;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            matrix(row, column) = number(a[row * 4 + column]);
    return matrix;
}
}

QJsonObject Scene::toJson() const
{
    QHash<const SceneObject*, int> indices;
    for (int i = 0; i < int(objects_.size()); ++i) indices.insert(objects_[i].get(), i);

    QJsonArray objects;
    for (const auto& object : objects_)
    {
        QJsonObject data;
        data["name"] = QString::fromStdString(object->name());
        data["parent"] = indices.value(object->parent(), -1);
        auto* transform = object->getComponent<TransformComponent>();
        data["position"] = vectorJson(transform->position);
        data["rotation"] = vectorJson(transform->rotation);
        data["scale"] = vectorJson(transform->scale);

        if (auto* light = object->getComponent<LightComponent>()) data["light"] = light->toJson();
        auto* terrain = object->getComponent<TerrainComponent>();
        if (terrain) data["terrain"] = terrain->toJson();
        if (auto* component = object->getComponent<MeshComponent>(); component && !terrain)
        {
            QJsonObject mesh;
            mesh["source"] = int(component->source());
            mesh["assetPath"] = QString::fromStdString(component->assetPath());
            QJsonArray vertices;
            QJsonArray indices;
            if (component->mesh)
            {
                for (float value : component->mesh->vertices) vertices.append(value);
                for (unsigned int value : component->mesh->indices) indices.append(double(value));
            }
            mesh["vertices"] = vertices;
            mesh["indices"] = indices;
            mesh["hasData"] = bool(component->mesh);
            data["mesh"] = mesh;
        }
        if (auto* component = object->getComponent<MaterialComponent>())
        {
            if (component->material)
            {
                const auto& material = *component->material;
                QJsonObject parameters;
                for (auto it = material.parameters.begin(); it != material.parameters.end(); ++it)
                    parameters[it.key()] = parameterJson(it.value());
                QJsonObject m;
                m["name"] = material.name;
                m["shader"] = material.shaderPath;
                m["parameters"] = parameters;
                data["material"] = m;
            }
            else data["material"] = QJsonValue::Null;
        }
        objects.append(data);
    }
    return {{"version", 1}, {"name", name_}, {"objects", objects}};
}

bool Scene::fromJson(const QJsonObject& data, QString& error)
{
    try
    {
        check(integer(data["version"]) == 1 && data["name"].isString() && data["objects"].isArray());
        auto entries = data["objects"].toArray();
        std::vector<std::unique_ptr<SceneObject>> loaded;
        for (const auto& entry : entries)
        {
            check(entry.isObject());
            auto o = entry.toObject();
            check(o["name"].isString());
            auto object = std::make_unique<SceneObject>(o["name"].toString().toStdString());
            auto* transform = object->getComponent<TransformComponent>();
            transform->position = readVector(o["position"]);
            transform->rotation = readVector(o["rotation"]);
            transform->scale = readVector(o["scale"]);
            if (o.contains("mesh"))
            {
                check(o["mesh"].isObject());
                auto m = o["mesh"].toObject();
                int source = integer(m["source"]);
                check(source >= 0 && source <= 3 && m["hasData"].isBool() && m["assetPath"].isString());
                check(m["vertices"].isArray() && m["indices"].isArray());
                std::unique_ptr<Mesh> mesh;
                if (m["hasData"].toBool())
                {
                    mesh = std::make_unique<Mesh>();
                    for (const auto& v : m["vertices"].toArray()) mesh->vertices.push_back(number(v));
                    check(mesh->vertices.size() % 8 == 0);
                    for (const auto& v : m["indices"].toArray())
                    {
                        int index = integer(v);
                        check(index >= 0 && index < int(mesh->vertices.size() / 8));
                        mesh->indices.push_back(index);
                    }
                    check(mesh->indices.size() % 3 == 0);
                }
                object->addComponent<MeshComponent>().restoreMesh(
                    std::move(mesh), MeshComponent::Source(source), m["assetPath"].toString().toStdString());
            }
            if (o.contains("terrain"))
            {
                check(o["terrain"].isObject() && !o.contains("mesh"));
                auto& terrain = object->addComponent<TerrainComponent>();
                QString terrainError;
                if (!terrain.fromJson(o["terrain"].toObject(), terrainError))
                    throw std::runtime_error(terrainError.toStdString());
            }
            if (o.contains("light"))
            {
                check(o["light"].isObject());
                QString lightError;
                if (!object->addComponent<LightComponent>().fromJson(o["light"].toObject(), lightError))
                    throw std::runtime_error(lightError.toStdString());
            }
            if (o.contains("material"))
            {
                auto& component = object->addComponent<MaterialComponent>();
                if (!o["material"].isNull())
                {
                    check(o["material"].isObject());
                    auto m = o["material"].toObject();
                    check(m["name"].isString() && m["shader"].isString() && m["parameters"].isObject());
                    component.material = std::make_shared<Material>();
                    component.material->name = m["name"].toString();
                    component.material->shaderPath = m["shader"].toString();
                    auto parameters = m["parameters"].toObject();
                    for (auto it = parameters.begin(); it != parameters.end(); ++it)
                    {
                        check(it.value().isObject());
                        if (!Material::isEngineUniform(it.key()))
                            component.material->parameters.insert(it.key(), readParameter(it.value().toObject()));
                    }
                }
            }
            loaded.push_back(std::move(object));
        }
        // All objects must exist before assigning parents.
        for (int i = 0; i < int(loaded.size()); ++i)
        {
            int parent = integer(entries[i].toObject()["parent"]);
            check(parent >= -1 && parent < int(loaded.size()));
            if (parent >= 0) check(loaded[i]->setParent(loaded[parent].get()));
        }
        // Only replace the current scene after validation succeeds.
        emit objectsAboutToReset();
        selection.reset();
        objects_ = std::move(loaded);
        name_ = data["name"].toString();
        emit sceneChanged();
        error.clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

void Scene::resetHistory()
{
    history_ = {toJson()};
    historyIndex_ = 0;
    savedState_ = history_[0];
    emit historyChanged();
}

void Scene::recordEdit()
{
    auto state = toJson();
    if (historyIndex_ >= 0 && history_[historyIndex_] == state) return;
    history_.resize(historyIndex_ + 1); // A new edit discards the redo branch.
    history_.push_back(state);
    if (history_.size() > 100) history_.erase(history_.begin());
    historyIndex_ = int(history_.size()) - 1;
    emit historyChanged();
}

bool Scene::undo()
{
    if (!canUndo()) return false;
    QString error;
    if (!fromJson(history_[historyIndex_ - 1], error)) return false;
    --historyIndex_;
    emit historyChanged();
    return true;
}

bool Scene::redo()
{
    if (!canRedo()) return false;
    QString error;
    if (!fromJson(history_[historyIndex_ + 1], error)) return false;
    ++historyIndex_;
    emit historyChanged();
    return true;
}

bool Scene::isModified() const { return toJson() != savedState_; }

bool Scene::save(const QString& path, QString& error)
{
    auto state = toJson();
    QSaveFile file(path);
    QByteArray bytes = QJsonDocument(state).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        error = file.errorString();
        return false;
    }
    filePath_ = QFileInfo(path).absoluteFilePath();
    savedState_ = state;
    error.clear();
    emit historyChanged();
    return true;
}

bool Scene::load(const QString& path, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    QJsonParseError parseError;
    auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        error = "Invalid scene JSON: " + parseError.errorString();
        return false;
    }
    if (!fromJson(document.object(), error)) return false;
    filePath_ = QFileInfo(path).absoluteFilePath();
    resetHistory();
    return true;
}
