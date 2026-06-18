#include "Editor/InspectorReflection.h"

#include "Framework/Math/Math.h"
#include "Framework/Scene/Component.h"
#include "Framework/Scene/Reflection.h"

#include <cstddef>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
#if NS_EDITOR_ENABLED
    bool DrawReflectedComponent(NS::Scene::Component& comp) noexcept
    {
        const NS::Scene::ReflectionInfo* info = comp.GetReflection();
        if (info == nullptr)
            return false;

        bool changed = false;
        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            const NS::Scene::FieldDesc& field = info->fields[i];
            switch (field.type)
            {
            case NS::Scene::FieldType::Float:
            {
                float value = 0.0f;
                field.get(&comp, &value);
                if (ImGui::DragFloat(field.name, &value, 0.05f))
                {
                    field.set(&comp, &value);
                    changed = true;
                }
                break;
            }
            case NS::Scene::FieldType::Int:
            {
                int value = 0;
                field.get(&comp, &value);
                if (ImGui::DragInt(field.name, &value))
                {
                    field.set(&comp, &value);
                    changed = true;
                }
                break;
            }
            case NS::Scene::FieldType::Bool:
            {
                bool value = false;
                field.get(&comp, &value);
                if (ImGui::Checkbox(field.name, &value))
                {
                    field.set(&comp, &value);
                    changed = true;
                }
                break;
            }
            case NS::Scene::FieldType::Vector3:
            {
                NS::Math::Vector3 value{};
                field.get(&comp, &value);
                float xyz[3] = {value.x, value.y, value.z};
                if (ImGui::DragFloat3(field.name, xyz, 0.05f))
                {
                    value = NS::Math::Vector3{xyz[0], xyz[1], xyz[2]};
                    field.set(&comp, &value);
                    changed = true;
                }
                break;
            }
            }
        }
        return changed;
    }
#else
    bool DrawReflectedComponent(NS::Scene::Component&) noexcept
    {
        return false;
    }
#endif
} // namespace NS::Editor
