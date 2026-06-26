#include "Editor/ComponentClipboard.h"

#include "Framework/Math/Math.h"
#include "Framework/Scene/Component.h"
#include "Framework/Scene/Reflection.h"

#include <cstddef>
#include <string>
#include <utility>

namespace NS::Editor
{
    NS::Game::Level::ComponentData CaptureComponentData(const NS::Scene::Component& comp)
    {
        NS::Game::Level::ComponentData data;
        const NS::Scene::ReflectionInfo* info = comp.GetReflection();
        if (info == nullptr)
            return data;

        data.typeName = info->typeName;
        data.fields.reserve(info->fieldCount);
        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            const NS::Scene::FieldDesc& field = info->fields[i];
            NS::Game::Level::FieldValue value;
            value.name = field.name;
            switch (field.type)
            {
            case NS::Scene::FieldType::Float:
            {
                float v = 0.0f;
                field.get(&comp, &v);
                value.value = v;
                break;
            }
            case NS::Scene::FieldType::Int:
            {
                int v = 0;
                field.get(&comp, &v);
                value.value = v;
                break;
            }
            case NS::Scene::FieldType::Bool:
            {
                bool v = false;
                field.get(&comp, &v);
                value.value = v;
                break;
            }
            case NS::Scene::FieldType::Vector3:
            {
                NS::Math::Vector3 v{};
                field.get(&comp, &v);
                value.value = v;
                break;
            }
            case NS::Scene::FieldType::String:
            {
                std::string v;
                field.get(&comp, &v);
                value.value = std::move(v);
                break;
            }
            default:
                // 未対応の FieldType は値を取り違えるより写さない方が安全
                continue;
            }
            data.fields.push_back(std::move(value));
        }
        return data;
    }
} // namespace NS::Editor
