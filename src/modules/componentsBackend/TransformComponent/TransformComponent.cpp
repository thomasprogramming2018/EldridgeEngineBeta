#include "TransformComponent.h"
#include "../../scene/SceneObject.h"

QMatrix4x4 TransformComponent::getModelMatrix() const
{
    QMatrix4x4 model;
    model.translate(position);
    model.rotate(rotation.z(), 0, 0, 1);
    model.rotate(rotation.y(), 0, 1, 0);
    model.rotate(rotation.x(), 1, 0, 0);
    model.scale(scale);

    if (auto* parent = owner_.parent())
    {
        if (auto* transform = parent->getComponent<TransformComponent>())
            model = transform->getModelMatrix() * model;
    }

    return model;
}
