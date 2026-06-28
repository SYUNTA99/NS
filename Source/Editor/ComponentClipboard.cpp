#include "Editor/ComponentClipboard.h"

#include "Framework/Math/Math.h"
#include "Framework/Scene/Component.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Reflection.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

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
                // 未対応の FieldType は取り違えるより写さない方が安全
                continue;
            }
            data.fields.push_back(std::move(value));
        }
        return data;
    }

    void WriteBackComponentEdits(NS::Scene::GameObject& runtime, NS::Game::Level::ObjectInstance& object)
    {
        for (NS::Scene::Component* comp : runtime.Components())
        {
            if (comp == nullptr)
                continue;
            const NS::Scene::ReflectionInfo* info = comp->GetReflection();
            if (info == nullptr)
                continue;

            NS::Game::Level::ComponentData captured = CaptureComponentData(*comp);
            for (NS::Game::Level::ComponentData& data : object.components)
            {
                if (data.typeName != captured.typeName)
                    continue;
                data.fields = std::move(captured.fields); // 同型の最初の 1 件へ反射値ごと写す
                break;
            }
        }
    }
} // namespace NS::Editor
