#include "Editor/InspectorReflection.h"

#include "Editor/EditorUi.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/Curve.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <climits>
#include <cstring>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
    namespace
    {
        // 同じ欄を 2 体から読んで見比べる
        template <class T>
        bool SameValue(const NS::Object::Component& a,
                       const NS::Object::Component& b,
                       const NS::Object::FieldDesc& field)
        {
            T lhs{};
            T rhs{};
            field.get(&a, &lhs);
            field.get(&b, &rhs);
            return lhs == rhs;
        }

        // 同じ欄を src から dst へ写す
        template <class T>
        void CopyValue(NS::Object::Component& dst, const NS::Object::Component& src, const NS::Object::FieldDesc& field)
        {
            T value{};
            field.get(&src, &value);
            field.set(&dst, &value);
        }
    } // namespace

    ComponentDefaults::ComponentDefaults() noexcept = default;
    ComponentDefaults::~ComponentDefaults() noexcept = default;

    const NS::Object::Component* ComponentDefaults::Find(std::string_view typeName)
    {
        for (const auto& [name, comp] : m_byType)
        {
            if (name == typeName)
                return comp;
        }
        if (!m_holder)
            m_holder = std::make_unique<NS::Object::GameObject>();

        // 既定コンストラクタで作っただけの 1 体。 未登録の型は nullptr が返り、 その答も控えて再試行しない
        NS::Object::Component* created = NS::Object::CreateComponent(typeName, *m_holder);
        m_byType.emplace_back(std::string(typeName), created);
        return created;
    }

    bool FieldDiffersFromDefault(const NS::Object::Component& comp,
                                 const NS::Object::Component* defaults,
                                 const NS::Object::FieldDesc& field) noexcept
    {
        if (defaults == nullptr)
            return false;
        switch (field.type)
        {
        case NS::Object::FieldType::Float:
            return !SameValue<float>(comp, *defaults, field);
        case NS::Object::FieldType::Int:
            return !SameValue<int>(comp, *defaults, field);
        case NS::Object::FieldType::Bool:
            return !SameValue<bool>(comp, *defaults, field);
        case NS::Object::FieldType::Vector3:
            return !SameValue<NS::Core::Vector3>(comp, *defaults, field);
        case NS::Object::FieldType::String:
            return !SameValue<std::string>(comp, *defaults, field);
        case NS::Object::FieldType::ObjectRef:
            return !SameValue<NS::Object::ObjectRef>(comp, *defaults, field);
        case NS::Object::FieldType::Curve:
            return !SameValue<NS::Object::Curve>(comp, *defaults, field);
        }
        return false;
    }

    void RevertFieldToDefault(NS::Object::Component& comp,
                              const NS::Object::Component& defaults,
                              const NS::Object::FieldDesc& field) noexcept
    {
        switch (field.type)
        {
        case NS::Object::FieldType::Float:
            CopyValue<float>(comp, defaults, field);
            break;
        case NS::Object::FieldType::Int:
            CopyValue<int>(comp, defaults, field);
            break;
        case NS::Object::FieldType::Bool:
            CopyValue<bool>(comp, defaults, field);
            break;
        case NS::Object::FieldType::Vector3:
            CopyValue<NS::Core::Vector3>(comp, defaults, field);
            break;
        case NS::Object::FieldType::String:
            CopyValue<std::string>(comp, defaults, field);
            break;
        case NS::Object::FieldType::ObjectRef:
            CopyValue<NS::Object::ObjectRef>(comp, defaults, field);
            break;
        case NS::Object::FieldType::Curve:
            CopyValue<NS::Object::Curve>(comp, defaults, field);
            break;
        }
    }

#if NS_EDITOR_ENABLED
    namespace
    {
        // コンポーネントの表示名を解決する
        const char* DisplayTypeName(const NS::Object::ReflectionInfo* info) noexcept
        {
            if (info != nullptr)
            {
                return info->typeName;
            }
            return "Component";
        }
    } // namespace

    ComponentEditResult DrawReflectedComponent(NS::Object::Component& comp,
                                               std::span<const ObjectRefOption> refOptions,
                                               const NS::Object::Component* defaults) noexcept
    {
        const NS::Object::ReflectionInfo* info = comp.GetReflection();
        if (info == nullptr || info->fieldCount == 0)
        {
            return ComponentEditResult{};
        }
        if (!BeginFieldTable("##fields"))
        {
            return ComponentEditResult{};
        }

        ComponentEditResult result;
        // リフレクションの欄を 1 つずつ ImGui ウィジェットへ落とす
        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            const NS::Object::FieldDesc& field = info->fields[i];
            const bool changed = FieldDiffersFromDefault(comp, defaults, field);
            const bool wasChanged = result.changed;
            ImGui::PushID(static_cast<int>(i));
            FieldRow(field.name);

            switch (field.type)
            {
            case NS::Object::FieldType::Float:
            {
                float value = 0.0f;
                field.get(&comp, &value);
                if (ImGui::DragFloat("##value", &value, 0.05f))
                {
                    field.set(&comp, &value);
                    result.changed = true;
                }
                break;
            }
            case NS::Object::FieldType::Int:
            {
                int value = 0;
                field.get(&comp, &value);
                if (ImGui::DragInt("##value", &value))
                {
                    field.set(&comp, &value);
                    result.changed = true;
                }
                break;
            }
            case NS::Object::FieldType::Bool:
            {
                bool value = false;
                field.get(&comp, &value);
                if (ImGui::Checkbox("##value", &value))
                {
                    field.set(&comp, &value);
                    result.changed = true;
                }
                break;
            }
            case NS::Object::FieldType::Vector3:
            {
                NS::Core::Vector3 value{};
                field.get(&comp, &value);
                float xyz[3] = {value.x, value.y, value.z};
                if (ImGui::DragFloat3("##value", xyz, 0.05f))
                {
                    value = NS::Core::Vector3{xyz[0], xyz[1], xyz[2]};
                    field.set(&comp, &value);
                    result.changed = true;
                }
                break;
            }
            case NS::Object::FieldType::String:
            {
                std::string value;
                field.get(&comp, &value);
                char buf[256];
                const std::size_t copied = value.copy(buf, sizeof(buf) - 1);
                buf[copied] = '\0';
                if (ImGui::InputText("##value", buf, sizeof(buf)))
                {
                    std::string edited(buf);
                    field.set(&comp, &edited);
                    result.changed = true;
                }
                break;
            }
            case NS::Object::FieldType::ObjectRef:
            {
                NS::Object::ObjectRef value{};
                field.get(&comp, &value);

                // 参照候補が無ければ id を直接打たせる
                if (refOptions.empty())
                {
                    int id = static_cast<int>(value.id);
                    const char* format = "未設定";
                    if (value.IsSet())
                    {
                        format = "id %d";
                    }
                    if (ImGui::DragInt("##value", &id, 1.0f, 0, INT_MAX, format))
                    {
                        if (id <= 0)
                        {
                            value.id = 0u;
                        }
                        else
                        {
                            value.id = static_cast<std::uint32_t>(id);
                        }
                        field.set(&comp, &value);
                        result.changed = true;
                    }
                    break;
                }

                // 参照候補リストから選択するためのコンボボックスを描画する
                const char* currentLabel = "未設定";
                if (value.IsSet())
                {
                    currentLabel = "(消えた参照)";
                }
                for (const ObjectRefOption& option : refOptions)
                {
                    if (option.id == value.id)
                    {
                        currentLabel = option.label.c_str();
                        break;
                    }
                }
                if (ImGui::BeginCombo("##value", currentLabel))
                {
                    if (ImGui::Selectable("未設定", !value.IsSet()))
                    {
                        value.id = 0;
                        field.set(&comp, &value);
                        result.changed = true;
                    }
                    for (const ObjectRefOption& option : refOptions)
                    {
                        ImGui::PushID(static_cast<int>(option.id));
                        if (ImGui::Selectable(option.label.c_str(), option.id == value.id))
                        {
                            value.id = option.id;
                            field.set(&comp, &value);
                            result.changed = true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            case NS::Object::FieldType::Curve:
            {
                NS::Object::Curve value{};
                field.get(&comp, &value);
                bool edited = false;
                for (std::uint32_t pointIndex = 0; pointIndex < value.count; ++pointIndex)
                {
                    ImGui::PushID(static_cast<int>(pointIndex));
                    float xy[2] = {value.keys[pointIndex].x, value.keys[pointIndex].y};
                    if (ImGui::DragFloat2("##point", xy, 0.05f))
                    {
                        value.keys[pointIndex] = NS::Object::Curve::Key{xy[0], xy[1]};
                        edited = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("削除"))
                    {
                        for (std::uint32_t next = pointIndex; next + 1 < value.count; ++next)
                        {
                            value.keys[next] = value.keys[next + 1];
                        }
                        --value.count;
                        value.keys[value.count] = NS::Object::Curve::Key{};
                        edited = true;
                        // 詰めた並びを同じ周回で触ると消した点の隣を重ねて編集するため、この周は抜ける
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                if (value.count < NS::Object::Curve::k_MaxKeys && ImGui::SmallButton("点を追加"))
                {
                    // 離れた位置に湧くと並びが崩れて SortKeys で点の番号が飛ぶため、末尾の点の右隣へ足す
                    float lastX = 0.0f;
                    float lastY = 1.0f;
                    if (value.count > 0)
                    {
                        lastX = value.keys[value.count - 1].x;
                        lastY = value.keys[value.count - 1].y;
                    }
                    value.keys[value.count] = NS::Object::Curve::Key{lastX + 1.0f, lastY};
                    ++value.count;
                    edited = true;
                }
                if (edited)
                {
                    // x を左右へドラッグすると並びが崩れるため、編集のたびに並べ直してから書き戻す
                    value.SortKeys();
                    field.set(&comp, &value);
                    result.changed = true;
                }
                break;
            }
            }

            result.activated |= ImGui::IsItemActivated();
            // 編集無しのクリックでもラッチを解くため、 確定ではなく非活性化で committed を立てる
            // 空編集は CommitComponentEdit が before==after で弾くので履歴は汚れない
            result.committed |= ImGui::IsItemDeactivated();

            if (result.changed && !wasChanged)
            {
                result.changedTarget = &comp;
                result.changedField = &field;
            }

            if (RevertButton(changed) && defaults != nullptr)
            {
                result.revertTarget = &comp;
                result.revertField = &field;
            }
            ImGui::PopID();
        }
        EndFieldTable();
        return result;
    }

    ComponentEditResult DrawObjectComponents(NS::Object::GameObject& obj,
                                             std::span<const ObjectRefOption> refOptions,
                                             ComponentDefaults* defaults) noexcept
    {
        ComponentEditResult result;
        int index = 0;
        for (NS::Object::Component* comp : obj.Components())
        {
            if (comp == nullptr)
            {
                continue;
            }
            const NS::Object::ReflectionInfo* info = comp->GetReflection();

            // Transform は Inspector 上部の専用パネルが編集するので、 リフレクション一覧では重複させない
            if (info != nullptr && std::strcmp(info->typeName, "TransformComponent") == 0)
            {
                continue;
            }

            ImGui::PushID(index++);
            ImGuiTreeNodeFlags flags = 0;
            if (info != nullptr)
            {
                flags = ImGuiTreeNodeFlags_DefaultOpen;
            }

            // コンポーネントごとのヘッダを描画する
            ImGui::PushStyleColor(ImGuiCol_Header, k_ComponentHeaderColor);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, k_ComponentHeaderHoveredColor);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, k_ComponentHeaderActiveColor);
            const bool open = ImGui::CollapsingHeader(DisplayTypeName(info), flags);
            ImGui::PopStyleColor(3);

            if (open)
            {
                if (info != nullptr)
                {
                    const NS::Object::Component* baseline = nullptr;
                    if (defaults != nullptr)
                    {
                        baseline = defaults->Find(info->typeName);
                    }
                    const ComponentEditResult r = DrawReflectedComponent(*comp, refOptions, baseline);
                    result.changed |= r.changed;
                    result.activated |= r.activated;
                    result.committed |= r.committed;
                    if (r.revertField != nullptr)
                    {
                        result.revertTarget = r.revertTarget;
                        result.revertField = r.revertField;
                    }
                    if (r.changedField != nullptr)
                    {
                        result.changedTarget = r.changedTarget;
                        result.changedField = r.changedField;
                    }
                }
                else
                {
                    ImGui::TextDisabled("調整できるパラメータなし");
                }
            }
            ImGui::PopID();
        }
        return result;
    }
#else
    ComponentEditResult DrawReflectedComponent(NS::Object::Component&,
                                               std::span<const ObjectRefOption>,
                                               const NS::Object::Component*) noexcept
    {
        return ComponentEditResult{};
    }

    ComponentEditResult DrawObjectComponents(NS::Object::GameObject&,
                                             std::span<const ObjectRefOption>,
                                             ComponentDefaults*) noexcept
    {
        return ComponentEditResult{};
    }
#endif
} // namespace NS::Editor
