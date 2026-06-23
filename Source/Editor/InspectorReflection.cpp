#include "Editor/InspectorReflection.h"

#include "Framework/Math/Math.h"
#include "Framework/Scene/Component.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Reflection.h"

#include <cstddef>
#include <typeinfo>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
#if NS_EDITOR_ENABLED
    namespace
    {
        // 見出しに使う型名。反射があればその typeName、無ければ RTTI 名から名前空間を剥がして使う
        // typeid はエディタ build 限定のここでしか触らないので、出荷ビルドへ RTTI 依存が漏れない
        const char* DisplayTypeName(const NS::Scene::Component& comp, const NS::Scene::ReflectionInfo* info) noexcept
        {
            if (info != nullptr)
                return info->typeName;

            const char* raw = typeid(comp).name(); // MSVC は "class NS::Scene::HazardComponent" を返す
            const char* name = raw;
            for (const char* p = raw; *p != '\0'; ++p)
            {
                if (p[0] == ':' && p[1] == ':')
                    name = p + 2;
            }
            return name;
        }
    } // namespace

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

    bool DrawObjectComponents(NS::Scene::GameObject& obj) noexcept
    {
        bool changed = false;
        int index = 0;
        for (NS::Scene::Component* comp : obj.Components())
        {
            if (comp == nullptr)
                continue;
            const NS::Scene::ReflectionInfo* info = comp->GetReflection();

            // 反射が無い Component も見出しは必ず出す。何が乗っているか一覧できることを優先する
            // 反射ありは既定で開いて編集 UI を見せ、反射なしは畳んだ見出しだけにして雑然とさせない
            ImGui::PushID(index++);
            const ImGuiTreeNodeFlags flags = (info != nullptr) ? ImGuiTreeNodeFlags_DefaultOpen : 0;
            if (ImGui::CollapsingHeader(DisplayTypeName(*comp, info), flags))
            {
                if (info != nullptr)
                {
                    if (DrawReflectedComponent(*comp))
                        changed = true;
                }
                else
                {
                    ImGui::TextDisabled("調整できるパラメータなし");
                }
            }
            ImGui::PopID();
        }
        return changed;
    }
#else
    bool DrawReflectedComponent(NS::Scene::Component&) noexcept
    {
        return false;
    }

    bool DrawObjectComponents(NS::Scene::GameObject&) noexcept
    {
        return false;
    }
#endif
} // namespace NS::Editor
