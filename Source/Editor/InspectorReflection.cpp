#include "Editor/InspectorReflection.h"

#include <climits>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
#if NS_EDITOR_ENABLED
    namespace
    {
        // 見出しに使う型名。実行時型情報はビルド設定で切っているため、反射の無い component は総称で出す
        const char* DisplayTypeName(const NS::Scene::ReflectionInfo* info) noexcept
        {
            if (info != nullptr)
                return info->typeName;
            return "Component";
        }
    } // namespace

    bool DrawReflectedComponent(NS::Scene::Component& comp, std::span<const ObjectRefOption> refOptions) noexcept
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
            case NS::Scene::FieldType::String:
            {
                std::string value;
                field.get(&comp, &value);
                char buf[256];
                const std::size_t copied = value.copy(buf, sizeof(buf) - 1);
                buf[copied] = '\0';
                if (ImGui::InputText(field.name, buf, sizeof(buf)))
                {
                    std::string edited(buf);
                    field.set(&comp, &edited);
                    changed = true;
                }
                break;
            }
            case NS::Scene::FieldType::ObjectRef:
            {
                NS::Scene::ObjectRef value{};
                field.get(&comp, &value);

                // 候補が無い文脈では永続 id の数値入力に落とす
                if (refOptions.empty())
                {
                    int id = static_cast<int>(value.id);
                    const char* format = "未設定";
                    if (value.IsSet())
                        format = "id %d";
                    if (ImGui::DragInt(field.name, &id, 1.0f, 0, INT_MAX, format))
                    {
                        if (id <= 0)
                            value.id = 0u;
                        else
                            value.id = static_cast<std::uint32_t>(id);
                        field.set(&comp, &value);
                        changed = true;
                    }
                    break;
                }

                // レベル配置物から参照先を選ぶコンボ。現在値が候補に無い id なら消えた参照として明示する
                const char* currentLabel = "未設定";
                if (value.IsSet())
                    currentLabel = "(消えた参照)";
                for (const ObjectRefOption& option : refOptions)
                {
                    if (option.id == value.id)
                    {
                        currentLabel = option.label.c_str();
                        break;
                    }
                }
                if (ImGui::BeginCombo(field.name, currentLabel))
                {
                    if (ImGui::Selectable("未設定", !value.IsSet()))
                    {
                        value.id = 0;
                        field.set(&comp, &value);
                        changed = true;
                    }
                    for (const ObjectRefOption& option : refOptions)
                    {
                        ImGui::PushID(static_cast<int>(option.id));
                        if (ImGui::Selectable(option.label.c_str(), option.id == value.id))
                        {
                            value.id = option.id;
                            field.set(&comp, &value);
                            changed = true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            }
        }
        return changed;
    }

    bool DrawObjectComponents(NS::Scene::GameObject& obj, std::span<const ObjectRefOption> refOptions) noexcept
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
            ImGuiTreeNodeFlags flags = 0;
            if (info != nullptr)
                flags = ImGuiTreeNodeFlags_DefaultOpen;
            if (ImGui::CollapsingHeader(DisplayTypeName(info), flags))
            {
                if (info != nullptr)
                {
                    if (DrawReflectedComponent(*comp, refOptions))
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
    bool DrawReflectedComponent(NS::Scene::Component&, std::span<const ObjectRefOption>) noexcept
    {
        return false;
    }

    bool DrawObjectComponents(NS::Scene::GameObject&, std::span<const ObjectRefOption>) noexcept
    {
        return false;
    }
#endif
} // namespace NS::Editor
