#include "Editor/InspectorReflection.h"

#include "Editor/EditorUi.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/Curve.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <algorithm>
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

        constexpr float k_CurveGraphHeight = 100.0f;
        constexpr float k_CurveGrabRadius = 6.0f;

        constexpr ImU32 k_CurveGridColor = IM_COL32(255, 255, 255, 28);
        constexpr ImU32 k_CurveRefLineColor = IM_COL32(255, 255, 255, 70);
        constexpr ImU32 k_CurveLineColor = IM_COL32(150, 200, 255, 255);
        constexpr ImU32 k_CurvePointColor = IM_COL32(230, 230, 230, 255);
        constexpr ImU32 k_CurvePointActiveColor = IM_COL32(255, 200, 80, 255);

        struct CurveGraphView
        {
            ImVec2 origin{};
            ImVec2 size{};
            float yMin = 0.0f;
            float yMax = 1.0f;
        };

        // y の表示範囲を点の最小最大から決める。最低でも 0..1 を含め、上下に 1 割の余白を足す
        void ComputeCurveYRange(const NS::Object::Curve& curve, float& outMin, float& outMax) noexcept
        {
            float low = 0.0f;
            float high = 1.0f;
            for (std::uint32_t i = 0; i < curve.count; ++i)
            {
                low = std::min(low, curve.keys[i].y);
                high = std::max(high, curve.keys[i].y);
            }
            const float margin = (high - low) * 0.1f;
            outMin = low - margin;
            outMax = high + margin;
        }

        // カーブ座標を画面ピクセルへ写す。画面の y 軸は下向きなので上下を反転する
        ImVec2 CurveToScreen(const CurveGraphView& view, float x, float y) noexcept
        {
            const float ratioY = (y - view.yMin) / (view.yMax - view.yMin);
            return ImVec2{view.origin.x + x * view.size.x, view.origin.y + (1.0f - ratioY) * view.size.y};
        }

        // 画面ピクセルをカーブ座標へ戻す。今の使い手の入力域が 0..1 のため x はそこへ収める
        NS::Object::Curve::Key ScreenToCurve(const CurveGraphView& view, ImVec2 pos) noexcept
        {
            const float x = std::clamp((pos.x - view.origin.x) / view.size.x, 0.0f, 1.0f);
            const float ratioY = 1.0f - (pos.y - view.origin.y) / view.size.y;
            return NS::Object::Curve::Key{x, view.yMin + ratioY * (view.yMax - view.yMin)};
        }

        int FindKeyNear(const NS::Object::Curve& curve, const CurveGraphView& view, ImVec2 pos, float radius) noexcept
        {
            int nearest = -1;
            float nearestSq = radius * radius;
            for (std::uint32_t i = 0; i < curve.count; ++i)
            {
                const ImVec2 center = CurveToScreen(view, curve.keys[i].x, curve.keys[i].y);
                const float dx = center.x - pos.x;
                const float dy = center.y - pos.y;
                const float distSq = dx * dx + dy * dy;
                if (distSq <= nearestSq)
                {
                    nearestSq = distSq;
                    nearest = static_cast<int>(i);
                }
            }
            return nearest;
        }

        // ドラッグ中の SortKeys は掴んでいる点の番号を飛ばすため、動かした点だけ入れ替えて移動先の番号を返す
        std::uint32_t MoveKey(NS::Object::Curve& curve, std::uint32_t index, NS::Object::Curve::Key newKey) noexcept
        {
            curve.keys[index] = newKey;
            while (index > 0 && curve.keys[index - 1].x > newKey.x)
            {
                curve.keys[index] = curve.keys[index - 1];
                curve.keys[index - 1] = newKey;
                --index;
            }
            while (index + 1 < curve.count && curve.keys[index + 1].x < newKey.x)
            {
                curve.keys[index] = curve.keys[index + 1];
                curve.keys[index + 1] = newKey;
                ++index;
            }
            return index;
        }

        void RemoveKey(NS::Object::Curve& curve, std::uint32_t index) noexcept
        {
            for (std::uint32_t next = index; next + 1 < curve.count; ++next)
            {
                curve.keys[next] = curve.keys[next + 1];
            }
            --curve.count;
            curve.keys[curve.count] = NS::Object::Curve::Key{};
        }

        void DrawCurveGraph(ImDrawList& drawList,
                            const CurveGraphView& view,
                            const NS::Object::Curve& curve,
                            int highlighted) noexcept
        {
            const ImVec2 rectMin = view.origin;
            const ImVec2 rectMax{view.origin.x + view.size.x, view.origin.y + view.size.y};
            drawList.AddRectFilled(rectMin, rectMax, ImGui::GetColorU32(ImGuiCol_FrameBg));

            // 数値欄から 0..1 の外の x を打てるため、はみ出す線と点は描画域で切る
            drawList.PushClipRect(ImVec2{rectMin.x - k_CurveGrabRadius, rectMin.y},
                                  ImVec2{rectMax.x + k_CurveGrabRadius, rectMax.y},
                                  true);

            for (int i = 1; i < 4; ++i)
            {
                const float x = rectMin.x + view.size.x * static_cast<float>(i) * 0.25f;
                drawList.AddLine(ImVec2{x, rectMin.y}, ImVec2{x, rectMax.y}, k_CurveGridColor);
            }
            // y の範囲は点に合わせて動くため、0 と 1 の基準線を目盛りの代わりにする
            const float zeroY = CurveToScreen(view, 0.0f, 0.0f).y;
            const float oneY = CurveToScreen(view, 0.0f, 1.0f).y;
            drawList.AddLine(ImVec2{rectMin.x, zeroY}, ImVec2{rectMax.x, zeroY}, k_CurveRefLineColor);
            drawList.AddLine(ImVec2{rectMin.x, oneY}, ImVec2{rectMax.x, oneY}, k_CurveRefLineColor);

            if (curve.count > 0)
            {
                // Evaluate は範囲の外を端の値で止めるので、見た目も両端まで水平に延ばす
                ImVec2 prev = CurveToScreen(view, 0.0f, curve.keys[0].y);
                for (std::uint32_t i = 0; i < curve.count; ++i)
                {
                    const ImVec2 point = CurveToScreen(view, curve.keys[i].x, curve.keys[i].y);
                    drawList.AddLine(prev, point, k_CurveLineColor, 2.0f);
                    prev = point;
                }
                const ImVec2 rightEnd = CurveToScreen(view, 1.0f, curve.keys[curve.count - 1].y);
                drawList.AddLine(prev, rightEnd, k_CurveLineColor, 2.0f);
            }

            for (std::uint32_t i = 0; i < curve.count; ++i)
            {
                const ImVec2 center = CurveToScreen(view, curve.keys[i].x, curve.keys[i].y);
                ImU32 color = k_CurvePointColor;
                float radius = 4.0f;
                if (static_cast<int>(i) == highlighted)
                {
                    color = k_CurvePointActiveColor;
                    radius = 5.5f;
                }
                drawList.AddCircleFilled(center, radius, color);
            }

            drawList.PopClipRect();
            drawList.AddRect(rectMin, rectMax, ImGui::GetColorU32(ImGuiCol_Border));
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

                CurveGraphView view{};
                view.size = ImVec2{ImGui::CalcItemWidth(), k_CurveGraphHeight};
                view.origin = ImGui::GetCursorScreenPos();
                ComputeCurveYRange(value, view.yMin, view.yMax);

                // 右クリックの削除でも IsItemDeactivated の確定が立つため、右ボタンも受ける
                ImGui::InvisibleButton(
                    "##graph", view.size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
                // ドラッグ全体を 1 回の undo にまとめるため、グラフの活性化と非活性化も集約へ入れる
                result.activated |= ImGui::IsItemActivated();
                result.committed |= ImGui::IsItemDeactivated();

                ImGuiStorage* storage = ImGui::GetStateStorage();
                const ImGuiID grabbedId = ImGui::GetID("graph-grabbed");
                const ImGuiID grabOffsetXId = ImGui::GetID("graph-grab-offset-x");
                const ImGuiID grabOffsetYId = ImGui::GetID("graph-grab-offset-y");
                const ImVec2 mouse = ImGui::GetMousePos();
                const bool hovered = ImGui::IsItemHovered();
                const bool leftHeld = ImGui::IsMouseDown(ImGuiMouseButton_Left);

                int highlighted = -1;
                if (hovered || ImGui::IsItemActive())
                {
                    highlighted = FindKeyNear(value, view, mouse, k_CurveGrabRadius);
                }

                if (ImGui::IsItemActivated())
                {
                    // ドラッグの途中で隣の点へ乗り移ると形が壊れるため、掴む点は押した瞬間に決めて離すまで追う
                    // 右ボタンの活性化で前回の番号が残ると、右を押したまま左を押した時に別の点を掴むため空にする
                    int grabbedNow = -1;
                    if (leftHeld)
                    {
                        grabbedNow = highlighted;
                    }
                    storage->SetInt(grabbedId, grabbedNow);
                    if (grabbedNow >= 0)
                    {
                        // 掴んだ瞬間に点がカーソルへ飛ぶのを避けるため、点の中心と押した位置のずれを控える
                        const NS::Object::Curve::Key& key = value.keys[grabbedNow];
                        const ImVec2 center = CurveToScreen(view, key.x, key.y);
                        storage->SetFloat(grabOffsetXId, center.x - mouse.x);
                        storage->SetFloat(grabOffsetYId, center.y - mouse.y);
                    }
                }

                // 左の単クリックは掴みに使うため、追加は空きのダブルクリック、削除は点の右クリックに分ける
                if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && highlighted < 0 &&
                    value.count < NS::Object::Curve::k_MaxKeys)
                {
                    const NS::Object::Curve::Key added = ScreenToCurve(view, mouse);
                    value.keys[value.count] = added;
                    ++value.count;
                    // 追加してから掴み直す手間を省くため、追加した点をそのまま掴んだ扱いにする
                    const std::uint32_t inserted = MoveKey(value, value.count - 1, added);
                    storage->SetInt(grabbedId, static_cast<int>(inserted));
                    storage->SetFloat(grabOffsetXId, 0.0f);
                    storage->SetFloat(grabOffsetYId, 0.0f);
                    highlighted = static_cast<int>(inserted);
                    edited = true;
                }
                else if (hovered && !leftHeld && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && highlighted >= 0)
                {
                    RemoveKey(value, static_cast<std::uint32_t>(highlighted));
                    highlighted = -1;
                    edited = true;
                }
                else if (ImGui::IsItemActive() && leftHeld)
                {
                    const int grabbed = storage->GetInt(grabbedId, -1);
                    if (grabbed >= 0 && static_cast<std::uint32_t>(grabbed) < value.count)
                    {
                        const ImVec2 target{mouse.x + storage->GetFloat(grabOffsetXId, 0.0f),
                                            mouse.y + storage->GetFloat(grabOffsetYId, 0.0f)};
                        const NS::Object::Curve::Key moved = ScreenToCurve(view, target);
                        if (moved != value.keys[grabbed])
                        {
                            const std::uint32_t movedTo = MoveKey(value, static_cast<std::uint32_t>(grabbed), moved);
                            storage->SetInt(grabbedId, static_cast<int>(movedTo));
                            highlighted = static_cast<int>(movedTo);
                            edited = true;
                        }
                    }
                }

                DrawCurveGraph(*ImGui::GetWindowDrawList(), view, value, highlighted);
                ImGui::TextDisabled("点をドラッグで移動 / 空きをダブルクリックで追加 / 点を右クリックで削除");

                // グラフのドラッグでは細かい値を打てないため、数値の並びを折り畳みで残す
                if (ImGui::TreeNode("数値で編集"))
                {
                    const float deleteButtonWidth =
                        ImGui::CalcTextSize("削除").x + ImGui::GetStyle().FramePadding.x * 2.0f;
                    for (std::uint32_t pointIndex = 0; pointIndex < value.count; ++pointIndex)
                    {
                        ImGui::PushID(static_cast<int>(pointIndex));
                        float xy[2] = {value.keys[pointIndex].x, value.keys[pointIndex].y};
                        ImGui::SetNextItemWidth(-(deleteButtonWidth + ImGui::GetStyle().ItemSpacing.x));
                        if (ImGui::DragFloat2("##point", xy, 0.05f))
                        {
                            value.keys[pointIndex] = NS::Object::Curve::Key{xy[0], xy[1]};
                            edited = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("削除"))
                        {
                            RemoveKey(value, pointIndex);
                            edited = true;
                            // 詰めた並びを同じ周回で回し続けると、消した点の隣の行がこの周だけ描かれないため抜ける
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
                        value.keys[value.count] = NS::Object::Curve::Key{lastX + 0.1f, lastY};
                        ++value.count;
                        edited = true;
                    }
                    ImGui::TreePop();
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
