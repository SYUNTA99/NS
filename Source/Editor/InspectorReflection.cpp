#include "Editor/InspectorReflection.h"
#include "NSlib/Core/Types.h"

#include "Editor/EditorAppearance.h"
#include "Editor/EditorUi.h"
#include "Editor/AssetPicker.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/Curve.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/Core/Windows/Filesystem.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS
{
    namespace Editor
    {
        bool ShowsOverride(const Object* comp,
                           const char* partKey,
                           const FieldDesc& field,
                           const SceneActor* actor)
        {
            NS_ASSERT(App, comp != nullptr, "ShowsOverride: 部品が null");
            if (actor == nullptr || partKey == nullptr ||
                FieldDestinationOf(actor, field) != FieldDestination::Scene)
            {
                return false;
            }
            return actor->IsSceneOverride(partKey, field.name);
        }

        void CurveGraphXRange(const Curve& curve, float& outMin, float& outMax) noexcept
        {
            float low = 0.0f;
            float high = 1.0f;
            for (uint32 i = 0; i < curve.count; ++i)
            {
                low = std::min(low, curve.keys[i].x);
                high = std::max(high, curve.keys[i].x);
            }
            // 0〜1 の曲線は余白を足さない。部品の欄の曲線の見た目を変えないため
            if (low < 0.0f || high > 1.0f)
            {
                high += (high - low) * 0.1f;
            }
            outMin = low;
            outMax = high;
        }

#if NS_EDITOR_ENABLED
        namespace
        {
            constexpr float k_CurveGraphHeight = 150.0f;
            constexpr float k_CurveGrabRadius = 10.0f;
            constexpr float k_CurveHandleLength = 40.0f;

            constexpr ImU32 k_CurveGridColor = IM_COL32(255, 255, 255, 28);
            constexpr ImU32 k_CurveRefLineColor = IM_COL32(255, 255, 255, 70);
            constexpr ImU32 k_CurveLineColor = IM_COL32(150, 200, 255, 255);
            constexpr ImU32 k_CurvePointColor = IM_COL32(230, 230, 230, 255);
            constexpr ImU32 k_CurvePointActiveColor = IM_COL32(255, 200, 80, 255);
            constexpr ImU32 k_CurveHandleColor = IM_COL32(150, 230, 170, 255);

            constexpr int k_GrabPoint = 0;
            constexpr int k_GrabInHandle = 1;
            constexpr int k_GrabOutHandle = 2;
            constexpr int k_GrabPan = 3;

            struct CurveGraphView
            {
                ImVec2 origin{};
                ImVec2 size{};
                float xMin = 0.0f;
                float xMax = 1.0f;
                float yMin = 0.0f;
                float yMax = 1.0f;
            };

            // 手動接線では点の間で膨らむため、全体表示は評価した曲線も含めて余白を足す
            void ComputeCurveYRange(const Curve& curve, float& outMin, float& outMax) noexcept
            {
                float low = 0.0f;
                float high = 1.0f;
                for (uint32 i = 0; i < curve.count; ++i)
                {
                    low = std::min(low, curve.keys[i].y);
                    high = std::max(high, curve.keys[i].y);
                }
                for (uint32 i = 0; i + 1 < curve.count; ++i)
                {
                    for (int sample = 1; sample < 64; ++sample)
                    {
                        const float x = curve.keys[i].x +
                                        (curve.keys[i + 1].x - curve.keys[i].x) * static_cast<float>(sample) / 64.0f;
                        const float y = curve.Evaluate(x);
                        low = std::min(low, y);
                        high = std::max(high, y);
                    }
                }
                const float margin = (high - low) * 0.1f;
                outMin = low - margin;
                outMax = high + margin;
            }

            float CurveGridStep(float span, float pixels, float spacing) noexcept
            {
                const float rawStep = span / std::max(1.0f, pixels / spacing);
                const float magnitude = std::pow(10.0f, std::floor(std::log10(rawStep)));
                const float fraction = rawStep / magnitude;
                if (fraction <= 1.0f)
                {
                    return magnitude;
                }
                if (fraction <= 2.0f)
                {
                    return magnitude * 2.0f;
                }
                if (fraction <= 5.0f)
                {
                    return magnitude * 5.0f;
                }
                return magnitude * 10.0f;
            }

            void StoreCurveView(ImGuiStorage& storage, const CurveGraphView& view) noexcept
            {
                storage.SetFloat(ImGui::GetID("graph-view-x-min"), view.xMin);
                storage.SetFloat(ImGui::GetID("graph-view-x-max"), view.xMax);
                storage.SetFloat(ImGui::GetID("graph-view-y-min"), view.yMin);
                storage.SetFloat(ImGui::GetID("graph-view-y-max"), view.yMax);
            }

            void ZoomCurveAxis(float& low, float& high, float anchor, float factor) noexcept
            {
                const float span = high - low;
                // 大きなフレーム数では小さすぎる幅が丸めで消えるため、値の桁にも下限を合わせる
                const float minSpan = std::max(0.0001f, std::abs(anchor) * std::numeric_limits<float>::epsilon() * 16.0f);
                const float maxSpan = std::max(minSpan, 10000000.0f);
                const float newSpan = std::clamp(span * factor, minSpan, maxSpan);
                const float ratio = (anchor - low) / span;
                low = anchor - ratio * newSpan;
                high = low + newSpan;
            }

            // カーブ座標を画面ピクセルへ写す。画面の y 軸は下向きなので上下を反転する
            ImVec2 CurveToScreen(const CurveGraphView& view, float x, float y) noexcept
            {
                const float ratioY = (y - view.yMin) / (view.yMax - view.yMin);
                const float ratioX = (x - view.xMin) / (view.xMax - view.xMin);
                return ImVec2{view.origin.x + ratioX * view.size.x, view.origin.y + (1.0f - ratioY) * view.size.y};
            }

            // 横方向へ欄を越えてドラッグした時は表示範囲の端で止める
            Curve::Key ScreenToCurve(const CurveGraphView& view, ImVec2 pos) noexcept
            {
                const float ratioX = std::clamp((pos.x - view.origin.x) / view.size.x, 0.0f, 1.0f);
                const float ratioY = 1.0f - (pos.y - view.origin.y) / view.size.y;
                return Curve::Key{view.xMin + ratioX * (view.xMax - view.xMin),
                                      view.yMin + ratioY * (view.yMax - view.yMin)};
            }

            // 昇格の初期値も表示も実際に効いている傾きから作る。ずれると掴んだ瞬間に形が飛ぶため
            float DisplayTangent(const Curve& curve, uint32 index, bool leftSide) noexcept
            {
                const Curve::Key& key = curve.keys[index];
                if (key.mode == Curve::InterpMode::Manual)
                {
                    if (leftSide)
                    {
                        return key.inTangent;
                    }
                    return key.outTangent;
                }
                if (key.mode == Curve::InterpMode::AutoSmooth)
                {
                    return curve.AutoTangentAt(index);
                }
                if (leftSide)
                {
                    if (index == 0)
                    {
                        return 0.0f;
                    }
                    const float width = curve.keys[index].x - curve.keys[index - 1].x;
                    if (width <= 0.0f)
                    {
                        return 0.0f;
                    }
                    return (curve.keys[index].y - curve.keys[index - 1].y) / width;
                }
                if (index + 1 >= curve.count)
                {
                    return 0.0f;
                }
                const float width = curve.keys[index + 1].x - curve.keys[index].x;
                if (width <= 0.0f)
                {
                    return 0.0f;
                }
                return (curve.keys[index + 1].y - curve.keys[index].y) / width;
            }

            // 接線の向きを画面座標へ写して一定の画面距離に置く。カーブ座標の距離だと棒の長さが暴れるため
            ImVec2 TangentHandleTip(const CurveGraphView& view, ImVec2 center, float slope, bool leftSide) noexcept
            {
                float directionX = view.size.x / (view.xMax - view.xMin);
                float directionY = -slope * view.size.y / (view.yMax - view.yMin);
                if (leftSide)
                {
                    directionX = -directionX;
                    directionY = -directionY;
                }
                const float length = std::sqrt(directionX * directionX + directionY * directionY);
                if (length <= 0.0f)
                {
                    return center;
                }
                const float scale = k_CurveHandleLength / length;
                return ImVec2{center.x + directionX * scale, center.y + directionY * scale};
            }

            float ScreenToTangent(const CurveGraphView& view, ImVec2 center, ImVec2 pos, bool leftSide) noexcept
            {
                const float rangeX = view.xMax - view.xMin;
                float deltaX = (pos.x - center.x) / view.size.x * rangeX;
                const float deltaY = (center.y - pos.y) / view.size.y * (view.yMax - view.yMin);
                // x の幅が 0 に近づくと傾きが発散するので、点の反対側へ回り込んでも符号ごと最小幅で止める
                const float k_MinDeltaX = 0.02f * rangeX;
                if (leftSide)
                {
                    deltaX = std::min(deltaX, -k_MinDeltaX);
                }
                else
                {
                    deltaX = std::max(deltaX, k_MinDeltaX);
                }
                return deltaY / deltaX;
            }

            int FindKeyNear(const Curve& curve, const CurveGraphView& view, ImVec2 pos, float radius) noexcept
            {
                int nearest = -1;
                float nearestSq = radius * radius;
                for (uint32 i = 0; i < curve.count; ++i)
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
            uint32 MoveKey(Curve& curve, uint32 index, Curve::Key newKey) noexcept
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

            void RemoveKey(Curve& curve, uint32 index) noexcept
            {
                std::copy(curve.keys + index + 1, curve.keys + curve.count, curve.keys + index);
                --curve.count;
                curve.keys[curve.count] = Curve::Key{};
            }

            void DrawCurveTangent(ImDrawList& drawList, ImVec2 center, ImVec2 tip) noexcept
            {
                const ImVec2 step{(tip.x - center.x) / 8.0f, (tip.y - center.y) / 8.0f};
                for (int i = 0; i < 8; i += 2)
                {
                    const float start = static_cast<float>(i);
                    const float end = static_cast<float>(i + 1);
                    drawList.AddLine(ImVec2{center.x + step.x * start, center.y + step.y * start},
                                     ImVec2{center.x + step.x * end, center.y + step.y * end},
                                     k_CurveHandleColor, 1.5f);
                }
                const ImVec2 low{tip.x - 5.0f, tip.y - 5.0f};
                const ImVec2 high{tip.x + 5.0f, tip.y + 5.0f};
                drawList.AddRectFilled(low, high, k_CurveHandleColor);
                drawList.AddRect(low, high, IM_COL32(20, 25, 30, 255));
            }

            void DrawCurveGraph(ImDrawList& drawList,
                                const CurveGraphView& view,
                                const Curve& curve,
                                int highlighted,
                                int selected) noexcept
            {
                const ImVec2 rectMin = view.origin;
                const ImVec2 rectMax{view.origin.x + view.size.x, view.origin.y + view.size.y};
                drawList.AddRectFilled(rectMin, rectMax, ImGui::GetColorU32(ImGuiCol_FrameBg));

                // 数値欄から 0..1 の外の x を打てるため、はみ出す線と点は描画域で切る
                drawList.PushClipRect(ImVec2{rectMin.x - k_CurveGrabRadius, rectMin.y},
                                      ImVec2{rectMax.x + k_CurveGrabRadius, rectMax.y},
                                      true);

                const float xStep = CurveGridStep(view.xMax - view.xMin, view.size.x, 80.0f);
                const float yStep = CurveGridStep(view.yMax - view.yMin, view.size.y, 40.0f);
                const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                const float xStart = std::ceil(view.xMin / xStep) * xStep;
                for (int i = 0; i < 100; ++i)
                {
                    const float tick = xStart + static_cast<float>(i) * xStep;
                    if (tick > view.xMax)
                    {
                        break;
                    }
                    const float x = CurveToScreen(view, tick, 0.0f).x;
                    drawList.AddLine(ImVec2{x, rectMin.y}, ImVec2{x, rectMax.y}, k_CurveGridColor);
                    char label[32]{};
                    ImFormatString(label, sizeof(label), "%.4g", tick + 0.0f);
                    const float labelWidth = ImGui::CalcTextSize(label).x;
                    const float labelX = std::clamp(x + 3.0f, rectMin.x, std::max(rectMin.x, rectMax.x - labelWidth));
                    drawList.AddText(ImVec2{labelX, rectMax.y - ImGui::GetFontSize() - 2.0f}, textColor, label);
                }
                const float yStart = std::ceil(view.yMin / yStep) * yStep;
                for (int i = 0; i < 100; ++i)
                {
                    const float tick = yStart + static_cast<float>(i) * yStep;
                    if (tick > view.yMax)
                    {
                        break;
                    }
                    const float y = CurveToScreen(view, 0.0f, tick).y;
                    ImU32 color = k_CurveGridColor;
                    if (std::abs(tick) < yStep * 0.01f)
                    {
                        color = k_CurveRefLineColor;
                    }
                    drawList.AddLine(ImVec2{rectMin.x, y}, ImVec2{rectMax.x, y}, color);
                    char label[32]{};
                    ImFormatString(label, sizeof(label), "%.4g", tick + 0.0f);
                    if (y > rectMin.y + ImGui::GetFontSize() && y < rectMax.y - ImGui::GetFontSize() * 2.0f)
                    {
                        drawList.AddText(ImVec2{rectMin.x + 3.0f, y + 2.0f}, textColor, label);
                    }
                }

                if (curve.count > 0)
                {
                    // Evaluate は範囲の外を端の値で止めるので、見た目も両端まで水平に延ばす
                    const ImVec2 firstPoint = CurveToScreen(view, curve.keys[0].x, curve.keys[0].y);
                    drawList.AddLine(
                        CurveToScreen(view, view.xMin, curve.keys[0].y), firstPoint, k_CurveLineColor, 2.0f);
                    for (uint32 i = 0; i + 1 < curve.count; ++i)
                    {
                        const Curve::Key& left = curve.keys[i];
                        const Curve::Key& right = curve.keys[i + 1];
                        ImVec2 prev = CurveToScreen(view, left.x, left.y);
                        const ImVec2 end = CurveToScreen(view, right.x, right.y);
                        const bool straight =
                            left.mode == Curve::InterpMode::Linear && right.mode == Curve::InterpMode::Linear;
                        if (straight || right.x - left.x <= 0.0f)
                        {
                            drawList.AddLine(prev, end, k_CurveLineColor, 2.0f);
                            continue;
                        }
                        // 曲線の区間は実物の Evaluate
                        // をサンプリングして描く。描画用の別実装だと見た目と効く形がずれるため
                        constexpr int k_CurveSegmentSamples = 16;
                        for (int sample = 1; sample < k_CurveSegmentSamples; ++sample)
                        {
                            const float ratio = static_cast<float>(sample) / static_cast<float>(k_CurveSegmentSamples);
                            const float x = left.x + (right.x - left.x) * ratio;
                            const ImVec2 point = CurveToScreen(view, x, curve.Evaluate(x));
                            drawList.AddLine(prev, point, k_CurveLineColor, 2.0f);
                            prev = point;
                        }
                        drawList.AddLine(prev, end, k_CurveLineColor, 2.0f);
                    }
                    const Curve::Key& lastKey = curve.keys[curve.count - 1];
                    const ImVec2 lastPoint = CurveToScreen(view, lastKey.x, lastKey.y);
                    drawList.AddLine(lastPoint, CurveToScreen(view, view.xMax, lastKey.y), k_CurveLineColor, 2.0f);
                }

                if (selected >= 0 && static_cast<uint32>(selected) < curve.count)
                {
                    const uint32 selectedIndex = static_cast<uint32>(selected);
                    const ImVec2 center = CurveToScreen(view, curve.keys[selectedIndex].x, curve.keys[selectedIndex].y);
                    if (selectedIndex > 0)
                    {
                        const ImVec2 tip = TangentHandleTip(view, center, DisplayTangent(curve, selectedIndex, true), true);
                        DrawCurveTangent(drawList, center, tip);
                    }
                    if (selectedIndex + 1 < curve.count)
                    {
                        const ImVec2 tip = TangentHandleTip(view, center, DisplayTangent(curve, selectedIndex, false), false);
                        DrawCurveTangent(drawList, center, tip);
                    }
                }

                for (uint32 i = 0; i < curve.count; ++i)
                {
                    const ImVec2 center = CurveToScreen(view, curve.keys[i].x, curve.keys[i].y);
                    ImU32 color = k_CurvePointColor;
                    float radius = 8.0f;
                    if (static_cast<int>(i) == highlighted)
                    {
                        color = k_CurvePointActiveColor;
                        radius = 9.0f;
                    }
                    drawList.AddCircleFilled(center, radius, color);
                    char number[8]{};
                    ImFormatString(number, sizeof(number), "%u", i + 1);
                    const ImVec2 textSize = ImGui::CalcTextSize(number);
                    drawList.AddText(ImVec2{center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f},
                                     IM_COL32(20, 25, 30, 255), number);
                    if (static_cast<int>(i) == selected)
                    {
                        drawList.AddCircle(center, radius + 2.5f, k_CurveHandleColor, 0, 1.5f);
                    }
                }

                drawList.PopClipRect();
                drawList.AddRect(rectMin, rectMax, ImGui::GetColorU32(ImGuiCol_Border));
            }
            // 欄 1 つのウィジェットで起きた事
            struct FieldWidgetResult
            {
                bool changed = false;   // 値が編集された
                bool activated = false; // 編集が始まった
                bool committed = false; // ウィジェットが非活性化した
                bool popupActive = false;
                bool wrote = false; // この描画で欄へ書こうとした
            };

            // 編集の間に最後に書けなかった理由を、欄のウィジェットの ID ごとに覚える鍵。書けたら Written に戻す
            constexpr const char* k_RejectedWriteKey = "field-write-rejected";

            // 書けた時だけ changed を立てる。書けなかった時は今の値が残るので、理由を覚えて離した時に知らせる
            void WriteField(void* owner, const FieldDesc& field, const void* value, FieldWidgetResult& result) noexcept
            {
                const FieldWriteResult written = field.set(owner, value);
                ImGui::GetStateStorage()->SetInt(ImGui::GetID(k_RejectedWriteKey), static_cast<int>(written));
                result.wrote = true;
                if (written == FieldWriteResult::Written)
                {
                    result.changed = true;
                }
            }

            // 入力を離した時に、編集の最後の書き込みが拒まれていれば WARN を 1 回出す。ドラッグの間は毎フレーム出さない
            // 編集が始まった描画で書いていなければ、前の編集の理由を捨てる。離す前に行が消えた編集の理由が、
            // 同じ ID の別のアクターの欄に出ないようにする
            void WarnRejectedWriteOnCommit(const FieldDesc& field, const FieldWidgetResult& result) noexcept
            {
                ImGuiStorage* storage = ImGui::GetStateStorage();
                const ImGuiID id = ImGui::GetID(k_RejectedWriteKey);
                if (result.activated && !result.wrote)
                {
                    storage->SetInt(id, static_cast<int>(FieldWriteResult::Written));
                }
                if (!result.committed)
                {
                    return;
                }
                const FieldWriteResult last =
                    static_cast<FieldWriteResult>(storage->GetInt(id, static_cast<int>(FieldWriteResult::Written)));
                storage->SetInt(id, static_cast<int>(FieldWriteResult::Written));
                if (last == FieldWriteResult::Written)
                {
                    return;
                }
                NS_LOG_WARN(App, "欄 {} に入れた値を書かず、今の値を残した: {}", FieldLabel(field), FieldWriteResultText(last));
            }

            void DrawStringFieldWidget(void* owner, const FieldDesc& field, FieldWidgetResult& result) noexcept
            {
                std::string value;
                field.get(owner, &value);
                const auto resize = [](ImGuiInputTextCallbackData* data) -> int {
                    std::string* text = static_cast<std::string*>(data->UserData);
                    text->resize(static_cast<std::size_t>(data->BufTextLen));
                    data->Buf = text->data();
                    return 0;
                };
                if (ImGui::InputTextWithHint("##value", "文字を入力", value.data(), value.capacity() + 1,
                                             ImGuiInputTextFlags_CallbackResize, resize, &value))
                {
                    WriteField(owner, field, &value, result);
                }
                if (ImGui::IsItemHovered() && !value.empty())
                {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
                    ImGui::TextUnformatted(value.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }

            void DrawAssetFieldWidget(void* owner, const FieldDesc& field, FieldWidgetResult& result) noexcept
            {
                // 開くたびに候補を取り直す。検索や表示だけではディスクを読み直さず、次の欄へ検索も持ち越さない
                static std::vector<AssetOption> options;
                static char search[256]{};
                std::string value;
                field.get(owner, &value);
                ImGuiStorage* storage = ImGui::GetStateStorage();
                const ImGuiID wasOpenId = ImGui::GetID("asset-picker-was-open");
                const bool wasOpen = storage->GetBool(wasOpenId, false);
                std::string label = OS::FileSystem::FileName(value);
                if (label.empty())
                {
                    label = "素材を選択...";
                }
                const std::string button = label + "###value";
                const float width = ImGui::CalcItemWidth();
                if (ImGui::Button(button.c_str(), ImVec2{std::max(width, 1.0f), 0.0f}))
                {
                    options = CollectAssetOptions(field.assetKind, OS::FileSystem::ContentRoot());
                    search[0] = '\0';
                    result.activated = true;
                    ImGui::OpenPopup("asset-picker");
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
                    ImGui::TextUnformatted("クリックして候補から選択");
                    if (!value.empty())
                    {
                        ImGui::TextUnformatted(value.c_str());
                    }
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                bool picked = false;
                ImGui::SetNextWindowSize(ImVec2{640.0f, 480.0f}, ImGuiCond_Appearing);
                if (ImGui::BeginPopup("asset-picker"))
                {
                    ImGui::TextUnformatted(FieldLabel(field));
                    if (ImGui::IsWindowAppearing())
                    {
                        ImGui::SetKeyboardFocusHere();
                    }
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputTextWithHint("##asset-search", "名前・フォルダーを検索", search, sizeof(search));
                    if (ImGui::SmallButton("未設定にする"))
                    {
                        if (!value.empty())
                        {
                            value.clear();
                            picked = true;
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::Separator();
                    bool found = false;
                    if (ImGui::BeginChild("##asset-options", ImVec2{0.0f, 0.0f}, ImGuiChildFlags_Borders))
                    {
                        for (const AssetOption& option : options)
                        {
                            if (!NameMatches(option.value.c_str(), search) && !NameMatches(option.label.c_str(), search) &&
                                !NameMatches(option.location.c_str(), search))
                            {
                                continue;
                            }
                            found = true;
                            ImGui::PushID(option.value.c_str());
                            const float line = ImGui::GetTextLineHeight();
                            const bool chosen = ImGui::Selectable("##option", option.value == value, 0,
                                                                  ImVec2{0.0f, line * 2.0f + 4.0f});
                            const ImVec2 at = ImGui::GetItemRectMin();
                            ImDrawList* drawList = ImGui::GetWindowDrawList();
                            drawList->AddText(ImVec2{at.x + 4.0f, at.y + 2.0f}, ImGui::GetColorU32(ImGuiCol_Text), option.label.c_str());
                            drawList->AddText(ImVec2{at.x + 4.0f, at.y + line + 3.0f}, ImGui::GetColorU32(ImGuiCol_TextDisabled), option.location.c_str());
                            if (ImGui::IsItemHovered())
                            {
                                ImGui::SetTooltip("%s", option.value.c_str());
                            }
                            ImGui::PopID();
                            if (chosen)
                            {
                                if (value != option.value)
                                {
                                    value = option.value;
                                    picked = true;
                                }
                                ImGui::CloseCurrentPopup();
                                break;
                            }
                        }
                        if (!found)
                        {
                            ImGui::TextWrapped("該当する素材がありません。ファイルをAssetsに追加してから開き直してください。");
                        }
                    }
                    ImGui::EndChild();
                    ImGui::EndPopup();
                }
                const bool open = ImGui::IsPopupOpen("asset-picker");
                result.popupActive = open;
                result.committed |= wasOpen && !open;
                storage->SetBool(wasOpenId, open);
                if (picked)
                {
                    WriteField(owner, field, &value, result);
                }
            }

            // 色の四角を width の幅で描き、押すと選ぶ画面を開く。選んだ色を value へ書いたら true
            bool DrawColorPicker(const FieldDesc& field, Color& value, float width, FieldWidgetResult& result) noexcept
            {
                float rgba[] = {value.x, value.y, value.z, value.w};
                // 色選択中はつまみを離しても編集を確定せず、選択画面を閉じるまでまとめる
                ImGuiStorage* storage = ImGui::GetStateStorage();
                const ImGuiID stateId = ImGui::GetID("##colorPickerOpen");
                const bool wasOpen = storage->GetBool(stateId, false);
                constexpr ImGuiColorEditFlags k_Flags = ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float |
                                                       ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_PickerHueBar |
                                                       ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_AlphaBar;
                if (ImGui::ColorButton("##value", ImVec4{value.x, value.y, value.z, value.w},
                                       ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoDragDrop |
                                           ImGuiColorEditFlags_NoTooltip,
                                       ImVec2{std::max(width, 1.0f), ImGui::GetFrameHeight() * 1.25f}))
                {
                    ImGui::OpenPopup("##colorPicker");
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("クリックして色・透明度を編集");
                }
                bool changed = false;
                if (ImGui::BeginPopup("##colorPicker"))
                {
                    ImGui::TextUnformatted(FieldLabel(field));
                    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20.0f);
                    changed = ImGui::ColorPicker4("##picker", rgba, k_Flags);
                    ImGui::EndPopup();
                }
                const bool open = ImGui::IsPopupOpen("##colorPicker");
                result.popupActive = open;
                result.activated |= open && !wasOpen;
                result.committed |= wasOpen && !open;
                storage->SetBool(stateId, open);
                if (changed)
                {
                    value = Color{rgba[0], rgba[1], rgba[2], rgba[3]};
                }
                return changed;
            }

            void DrawColorFieldWidget(void* owner, const FieldDesc& field, FieldWidgetResult& result) noexcept
            {
                Color value{};
                field.get(owner, &value);
                if (DrawColorPicker(field, value, ImGui::CalcItemWidth(), result))
                {
                    WriteField(owner, field, &value, result);
                }
            }

            // 色を入れるかのチェックボックスと色の四角を 1 つの欄に並べる。外すと空、入れると Color の既定の値から始まる
            void DrawOptionalColorFieldWidget(void* owner, const FieldDesc& field, FieldWidgetResult& result) noexcept
            {
                std::optional<Color> value;
                field.get(owner, &value);
                const float width = ImGui::CalcItemWidth();
                bool hasColor = value.has_value();
                if (ImGui::Checkbox("##has-value", &hasColor))
                {
                    if (hasColor)
                    {
                        value = Color{};
                    }
                    else
                    {
                        value.reset();
                    }
                    WriteField(owner, field, &value, result);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("チェックを入れると色を入れる。外すと空に戻す");
                }
                ImGui::SameLine();
                if (!value.has_value())
                {
                    ImGui::TextDisabled("未設定");
                    return;
                }
                const float pickerWidth = width - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x;
                Color color = *value;
                if (DrawColorPicker(field, color, pickerWidth, result))
                {
                    value = color;
                    WriteField(owner, field, &value, result);
                }
            }

            // ActorRef の欄。参照候補が無ければ id を打たせ、あれば候補から選ばせる
            // 書くのは候補の窓を閉じた後の 1 か所。書けなかった理由は欄の ID で覚えるので、窓の中では書かない
            void DrawActorRefFieldWidget(void* owner,
                                         const FieldDesc& field,
                                         std::span<const ActorRefOption> refOptions,
                                         FieldWidgetResult& result) noexcept
            {
                bool picked = false;
                ActorRef value{};
                field.get(owner, &value);

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
                            value.id = static_cast<uint32>(id);
                        }
                        WriteField(owner, field, &value, result);
                    }
                    return;
                }

                const char* currentLabel = "未設定";
                if (value.IsSet())
                {
                    currentLabel = "(消えた参照)";
                }
                for (const ActorRefOption& option : refOptions)
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
                        picked = true;
                    }
                    for (const ActorRefOption& option : refOptions)
                    {
                        ImGui::PushID(static_cast<int>(option.id));
                        if (ImGui::Selectable(option.label.c_str(), option.id == value.id))
                        {
                            value.id = option.id;
                            picked = true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                if (picked)
                {
                    WriteField(owner, field, &value, result);
                }
            }

            // 曲線のグラフが ImGui の状態の保存に使う識別子。グラフと同じ ID スタックの中で作る
            struct CurveGraphIds
            {
                ImGuiID grabbedId = 0;
                ImGuiID grabKindId = 0;
                ImGuiID grabOffsetXId = 0;
                ImGuiID grabOffsetYId = 0;
                ImGuiID selectedId = 0;
                ImGuiID menuPointId = 0;
                ImGuiID menuXId = 0;
                ImGuiID menuYId = 0;
                ImGuiID menuWasOpenId = 0;
            };

            CurveGraphIds MakeCurveGraphIds() noexcept
            {
                CurveGraphIds ids;
                ids.grabbedId = ImGui::GetID("graph-grabbed");
                ids.grabKindId = ImGui::GetID("graph-grab-kind");
                ids.grabOffsetXId = ImGui::GetID("graph-grab-offset-x");
                ids.grabOffsetYId = ImGui::GetID("graph-grab-offset-y");
                ids.selectedId = ImGui::GetID("graph-selected");
                ids.menuPointId = ImGui::GetID("graph-menu-point");
                ids.menuXId = ImGui::GetID("graph-menu-x");
                ids.menuYId = ImGui::GetID("graph-menu-y");
                ids.menuWasOpenId = ImGui::GetID("graph-menu-was-open");
                return ids;
            }

            // 曲線のグラフの右クリックのメニュー。値を変えたら true
            bool DrawCurveMenu(Curve& value, ImGuiStorage* storage, const CurveGraphIds& ids) noexcept
            {
                bool edited = false;
                if (ImGui::BeginPopup("curve-menu"))
                {
                    const int menuPoint = storage->GetInt(ids.menuPointId, -1);
                    if (menuPoint >= 0 && static_cast<uint32>(menuPoint) < value.count)
                    {
                        Curve::Key& key = value.keys[menuPoint];
                        const bool isLinear = key.mode == Curve::InterpMode::Linear;
                        const bool isSmooth = key.mode == Curve::InterpMode::AutoSmooth;
                        if (ImGui::MenuItem("直線にする", nullptr, isLinear) && !isLinear)
                        {
                            key.mode = Curve::InterpMode::Linear;
                            edited = true;
                        }
                        if (ImGui::MenuItem("なめらかにする", nullptr, isSmooth) && !isSmooth)
                        {
                            key.mode = Curve::InterpMode::AutoSmooth;
                            edited = true;
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("削除"))
                        {
                            RemoveKey(value, static_cast<uint32>(menuPoint));
                            storage->SetInt(ids.selectedId, -1);
                            edited = true;
                        }
                    }
                    else if (ImGui::MenuItem("点を追加", nullptr, false, value.count < Curve::k_MaxKeys))
                    {
                        const Curve::Key added{storage->GetFloat(ids.menuXId, 0.0f),
                                                   storage->GetFloat(ids.menuYId, 0.0f)};
                        value.keys[value.count] = added;
                        ++value.count;
                        const uint32 inserted = MoveKey(value, value.count - 1, added);
                        storage->SetInt(ids.selectedId, static_cast<int>(inserted));
                        edited = true;
                    }
                    ImGui::EndPopup();
                }
                return edited;
            }

            void TrackCurveItem(FieldWidgetResult& result) noexcept
            {
                result.activated |= ImGui::IsItemActivated();
                result.committed |= ImGui::IsItemDeactivated();
            }

            bool DrawCurveSelection(Curve& value, ImGuiStorage& storage,
                                    const CurveGraphIds& ids, FieldWidgetResult& result, float width) noexcept
            {
                int selected = storage.GetInt(ids.selectedId, -1);
                if (value.count > 0)
                {
                    const float right = ImGui::GetCursorScreenPos().x + width;
                    const float buttonSize = ImGui::GetFrameHeight();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("点");
                    ImGui::PushID("curve-key-buttons");
                    for (uint32 i = 0; i < value.count; ++i)
                    {
                        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + buttonSize <= right)
                        {
                            ImGui::SameLine();
                        }
                        const bool isSelected = selected == static_cast<int>(i);
                        if (isSelected)
                        {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                        }
                        char number[8]{};
                        ImFormatString(number, sizeof(number), "%u", i + 1);
                        if (ImGui::Button(number, ImVec2{buttonSize, buttonSize}))
                        {
                            selected = static_cast<int>(i);
                            storage.SetInt(ids.selectedId, selected);
                        }
                        if (isSelected)
                        {
                            ImGui::PopStyleColor();
                        }
                    }
                    ImGui::PopID();
                }
                if (selected < 0 || static_cast<uint32>(selected) >= value.count)
                {
                    if (value.count == 0)
                    {
                        ImGui::TextWrapped("空いている場所をダブルクリックして点を追加します。");
                    }
                    else
                    {
                        ImGui::TextWrapped("線は点の間の値を表します。番号か丸い点を選んで編集します。");
                    }
                    return false;
                }
                bool edited = false;
                ImGui::TextWrapped("丸を上下: 値を変更 / 左右: 位置を変更");
                if (value.count > 1)
                {
                    ImGui::TextWrapped("四角をドラッグ: 線の曲がり具合を変更");
                }
                ImGui::Text("点 %d (X / Y)", selected + 1);
                Curve::Key moved = value.keys[selected];
                float xy[2] = {moved.x, moved.y};
                ImGui::SetNextItemWidth(width);
                if (ImGui::DragFloat2("##selectedPoint", xy, 0.01f, 0.0f, 0.0f, "%.4f"))
                {
                    moved.x = xy[0];
                    moved.y = xy[1];
                    selected = static_cast<int>(MoveKey(value, static_cast<uint32>(selected), moved));
                    storage.SetInt(ids.selectedId, selected);
                    edited = true;
                }
                TrackCurveItem(result);
                const char* modes[] = {"直線", "自動なめらか", "手動接線"};
                ImGui::TextDisabled("線の形");
                const ImGuiID comboWasOpenId = ImGui::GetID("graph-mode-was-open");
                const bool comboWasOpen = storage.GetBool(comboWasOpenId, false);
                ImGui::SetNextItemWidth(width);
                const bool comboOpen = ImGui::BeginCombo("##interpolation", modes[static_cast<int>(value.keys[selected].mode)]);
                TrackCurveItem(result);
                storage.SetBool(comboWasOpenId, comboOpen);
                result.popupActive |= comboOpen;
                result.activated |= comboOpen && !comboWasOpen;
                if (!comboOpen && comboWasOpen)
                {
                    result.committed = true;
                }
                if (comboOpen)
                {
                    for (int mode = 0; mode < 3; ++mode)
                    {
                        if (ImGui::Selectable(modes[mode], mode == static_cast<int>(value.keys[selected].mode)))
                        {
                            Curve::Key& key = value.keys[selected];
                            if (mode == static_cast<int>(Curve::InterpMode::Manual) && key.mode != Curve::InterpMode::Manual)
                            {
                                key.inTangent = DisplayTangent(value, static_cast<uint32>(selected), true);
                                key.outTangent = DisplayTangent(value, static_cast<uint32>(selected), false);
                            }
                            key.mode = static_cast<Curve::InterpMode>(mode);
                            edited = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (value.keys[selected].mode == Curve::InterpMode::Manual)
                {
                    ImGui::TextDisabled("接線の傾き: 入 / 出");
                    Curve::Key& key = value.keys[selected];
                    float tangents[2] = {key.inTangent, key.outTangent};
                    ImGui::SetNextItemWidth(width);
                    if (ImGui::DragFloat2("##selectedTangents", tangents, 0.01f, 0.0f, 0.0f, "%.4f"))
                    {
                        key.inTangent = tangents[0];
                        key.outTangent = tangents[1];
                        edited = true;
                    }
                    TrackCurveItem(result);
                }
                if (ImGui::SmallButton("接線を水平に"))
                {
                    Curve::Key& key = value.keys[selected];
                    key.mode = Curve::InterpMode::Manual;
                    key.inTangent = 0.0f;
                    key.outTangent = 0.0f;
                    edited = true;
                }
                TrackCurveItem(result);
                const float buttonsWidth = ImGui::CalcTextSize("接線を水平に選択点を削除").x +
                                           ImGui::GetStyle().FramePadding.x * 4.0f + ImGui::GetStyle().ItemSpacing.x;
                if (width >= buttonsWidth)
                {
                    ImGui::SameLine();
                }
                if (ImGui::SmallButton("選択点を削除"))
                {
                    RemoveKey(value, static_cast<uint32>(selected));
                    storage.SetInt(ids.selectedId, -1);
                    edited = true;
                }
                TrackCurveItem(result);
                if (comboOpen)
                {
                    result.committed = false;
                }
                return edited;
            }

            // 一覧で比較して入力できるよう、全点の数値欄も折り畳みで残す
            bool DrawCurveNumbers(Curve& value, ImGuiStorage* storage, const CurveGraphIds& ids,
                                  FieldWidgetResult& result) noexcept
            {
                bool edited = false;
                if (ImGui::TreeNode("数値で編集"))
                {
                    const float deleteButtonWidth =
                        ImGui::CalcTextSize("削除").x + ImGui::GetStyle().FramePadding.x * 2.0f;
                    for (uint32 pointIndex = 0; pointIndex < value.count; ++pointIndex)
                    {
                        ImGui::PushID(static_cast<int>(pointIndex));
                        float xy[2] = {value.keys[pointIndex].x, value.keys[pointIndex].y};
                        ImGui::SetNextItemWidth(-(deleteButtonWidth + ImGui::GetStyle().ItemSpacing.x));
                        if (ImGui::DragFloat2("##point", xy, 0.05f))
                        {
                            // x y だけ書く。Key ごと作り直すとモードと接線が既定へ戻るため
                            value.keys[pointIndex].x = xy[0];
                            value.keys[pointIndex].y = xy[1];
                            edited = true;
                        }
                        TrackCurveItem(result);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("削除"))
                        {
                            TrackCurveItem(result);
                            RemoveKey(value, pointIndex);
                            storage->SetInt(ids.selectedId, -1);
                            edited = true;
                            // 詰めた並びを同じ周回で回し続けると、消した点の隣の行がこの周だけ描かれないため抜ける
                            ImGui::PopID();
                            break;
                        }
                        TrackCurveItem(result);
                        ImGui::PopID();
                    }
                    if (value.count < Curve::k_MaxKeys && ImGui::SmallButton("点を追加"))
                    {
                        // 離れた位置に湧くと並びが崩れて SortKeys で点の番号が飛ぶため、末尾の点の右隣へ足す
                        float lastX = 0.0f;
                        float lastY = 1.0f;
                        if (value.count > 0)
                        {
                            lastX = value.keys[value.count - 1].x;
                            lastY = value.keys[value.count - 1].y;
                        }
                        float rangeMin = 0.0f;
                        float rangeMax = 1.0f;
                        CurveGraphXRange(value, rangeMin, rangeMax);
                        value.keys[value.count] = Curve::Key{lastX + 0.1f * (rangeMax - rangeMin), lastY};
                        ++value.count;
                        edited = true;
                    }
                    TrackCurveItem(result);
                    ImGui::TreePop();
                }
                return edited;
            }

            // 曲線の欄。グラフのドラッグ・メニュー・数値の並びで点を編集する
            bool DrawCurveEditor(Curve& value, FieldWidgetResult& result, float width, float height) noexcept
            {
                bool edited = false;

                ImGuiStorage* storage = ImGui::GetStateStorage();
                const CurveGraphIds ids = MakeCurveGraphIds();
                const bool fit = ImGui::SmallButton("全体表示 (F)");
                CurveGraphView view{};
                view.size = ImVec2{std::max(width, 1.0f), height};
                view.origin = ImGui::GetCursorScreenPos();
                ComputeCurveYRange(value, view.yMin, view.yMax);
                CurveGraphXRange(value, view.xMin, view.xMax);
                const float xMargin = (view.xMax - view.xMin) * 0.05f;
                view.xMin -= xMargin;
                view.xMax += xMargin;
                const CurveGraphView fitted = view;
                // 移動した点で毎回範囲を変えると、同じカーソル位置でも値が動き続けるため保持する
                if (!fit)
                {
                    view.xMin = storage->GetFloat(ImGui::GetID("graph-view-x-min"), view.xMin);
                    view.xMax = storage->GetFloat(ImGui::GetID("graph-view-x-max"), view.xMax);
                    view.yMin = storage->GetFloat(ImGui::GetID("graph-view-y-min"), view.yMin);
                    view.yMax = storage->GetFloat(ImGui::GetID("graph-view-y-max"), view.yMax);
                }

                // 右クリックのメニュー操作でも活性化と確定を拾うため、右ボタンも受ける
                ImGui::InvisibleButton(
                    "##graph", view.size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                         ImGuiButtonFlags_MouseButtonMiddle);
                // ドラッグ全体を 1 回の undo にまとめるため、グラフの活性化と非活性化も集約へ入れる
                result.activated |= ImGui::IsItemActivated();
                const bool graphDeactivated = ImGui::IsItemDeactivated();

                const ImVec2 mouse = ImGui::GetMousePos();
                const bool hovered = ImGui::IsItemHovered();
                const bool leftHeld = ImGui::IsMouseDown(ImGuiMouseButton_Left);
                const ImGuiIO& io = ImGui::GetIO();
                if (hovered)
                {
                    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
                    if (!ImGui::IsAnyItemActive() && !io.WantTextInput)
                    {
                        if (ImGui::IsKeyPressed(ImGuiKey_F, false))
                        {
                            view = fitted;
                        }
                        if (io.MouseWheel != 0.0f)
                        {
                            const Curve::Key anchor = ScreenToCurve(view, mouse);
                            const float factor = std::pow(1.2f, -std::clamp(io.MouseWheel, -10.0f, 10.0f));
                            if (!io.KeyShift)
                            {
                                ZoomCurveAxis(view.xMin, view.xMax, anchor.x, factor);
                            }
                            if (!io.KeyCtrl)
                            {
                                ZoomCurveAxis(view.yMin, view.yMax, anchor.y, factor);
                            }
                        }
                    }
                }

                int highlighted = -1;
                if (hovered || ImGui::IsItemActive())
                {
                    highlighted = FindKeyNear(value, view, mouse, k_CurveGrabRadius);
                }

                // 点の削除は選択も解くが、既定へ戻すは点数だけ減らして選択を残すため、範囲の外へ出た選択は毎フレーム解く
                int selected = storage->GetInt(ids.selectedId, -1);
                if (selected >= 0 && static_cast<uint32>(selected) >= value.count)
                {
                    selected = -1;
                    storage->SetInt(ids.selectedId, -1);
                }

                if (ImGui::IsItemActivated())
                {
                    // ドラッグの途中で隣の点へ乗り移ると形が壊れるため、掴む点は押した瞬間に決めて離すまで追う
                    // 右ボタンの活性化で前回の番号が残ると、右を押したまま左を押した時に別の点を掴むため空にする
                    int grabbedNow = -1;
                    int grabKind = k_GrabPoint;
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (leftHeld && io.KeyAlt))
                    {
                        grabKind = k_GrabPan;
                    }
                    else if (leftHeld)
                    {
                        // 点よりハンドルを先に判定する。重なった時に点の掴みが勝つと選択が移って接線を触れないため
                        if (selected >= 0 && static_cast<uint32>(selected) < value.count)
                        {
                            const uint32 selectedIndex = static_cast<uint32>(selected);
                            const ImVec2 center =
                                CurveToScreen(view, value.keys[selectedIndex].x, value.keys[selectedIndex].y);
                            const ImVec2 inTip =
                                TangentHandleTip(view, center, DisplayTangent(value, selectedIndex, true), true);
                            const ImVec2 outTip =
                                TangentHandleTip(view, center, DisplayTangent(value, selectedIndex, false), false);
                            const float inDx = inTip.x - mouse.x;
                            const float inDy = inTip.y - mouse.y;
                            const float outDx = outTip.x - mouse.x;
                            const float outDy = outTip.y - mouse.y;
                            constexpr float k_HandleRadiusSq = k_CurveGrabRadius * k_CurveGrabRadius;
                            if (selectedIndex > 0 && inDx * inDx + inDy * inDy <= k_HandleRadiusSq)
                            {
                                grabbedNow = selected;
                                grabKind = k_GrabInHandle;
                            }
                            else if (selectedIndex + 1 < value.count && outDx * outDx + outDy * outDy <= k_HandleRadiusSq)
                            {
                                grabbedNow = selected;
                                grabKind = k_GrabOutHandle;
                            }
                        }
                        if (grabKind == k_GrabPoint)
                        {
                            grabbedNow = highlighted;
                            storage->SetInt(ids.selectedId, grabbedNow);
                            selected = grabbedNow;
                        }
                    }
                    storage->SetInt(ids.grabbedId, grabbedNow);
                    storage->SetInt(ids.grabKindId, grabKind);
                    if (grabbedNow >= 0 && grabKind == k_GrabPoint)
                    {
                        // 掴んだ瞬間に点がカーソルへ飛ぶのを避けるため、点の中心と押した位置のずれを控える
                        const Curve::Key& key = value.keys[grabbedNow];
                        const ImVec2 center = CurveToScreen(view, key.x, key.y);
                        storage->SetFloat(ids.grabOffsetXId, center.x - mouse.x);
                        storage->SetFloat(ids.grabOffsetYId, center.y - mouse.y);
                    }
                }

                // 左の単クリックは掴みと選択に使うため、追加は空きのダブルクリックか右クリックのメニューに分ける
                if (ImGui::IsItemActive() && storage->GetInt(ids.grabKindId) == k_GrabPan)
                {
                    if (!ImGui::IsItemActivated())
                    {
                        const float dx = io.MouseDelta.x / view.size.x * (view.xMax - view.xMin);
                        const float dy = io.MouseDelta.y / view.size.y * (view.yMax - view.yMin);
                        view.xMin -= dx;
                        view.xMax -= dx;
                        view.yMin += dy;
                        view.yMax += dy;
                    }
                }
                else if (hovered && !io.KeyAlt && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && highlighted < 0 &&
                    value.count < Curve::k_MaxKeys)
                {
                    const Curve::Key added = ScreenToCurve(view, mouse);
                    value.keys[value.count] = added;
                    ++value.count;
                    // 追加してから掴み直す手間を省くため、追加した点をそのまま掴んだ扱いにする
                    const uint32 inserted = MoveKey(value, value.count - 1, added);
                    storage->SetInt(ids.grabbedId, static_cast<int>(inserted));
                    storage->SetInt(ids.grabKindId, k_GrabPoint);
                    storage->SetInt(ids.selectedId, static_cast<int>(inserted));
                    storage->SetFloat(ids.grabOffsetXId, 0.0f);
                    storage->SetFloat(ids.grabOffsetYId, 0.0f);
                    highlighted = static_cast<int>(inserted);
                    edited = true;
                }
                else if (ImGui::IsItemActive() && leftHeld)
                {
                    const int grabbed = storage->GetInt(ids.grabbedId, -1);
                    const int grabKind = storage->GetInt(ids.grabKindId, k_GrabPoint);
                    if (grabbed >= 0 && static_cast<uint32>(grabbed) < value.count)
                    {
                        if (grabKind == k_GrabPoint)
                        {
                            const ImVec2 target{mouse.x + storage->GetFloat(ids.grabOffsetXId, 0.0f),
                                                mouse.y + storage->GetFloat(ids.grabOffsetYId, 0.0f)};
                            // 移動先の x y だけ写す。ScreenToCurve
                            // の返り値ごと代入するとモードと接線が既定へ戻るため
                            const Curve::Key targetKey = ScreenToCurve(view, target);
                            Curve::Key moved = value.keys[grabbed];
                            moved.x = targetKey.x;
                            moved.y = targetKey.y;
                            if (io.KeyCtrl)
                            {
                                const float xStep = CurveGridStep(view.xMax - view.xMin, view.size.x, 80.0f);
                                const float yStep = CurveGridStep(view.yMax - view.yMin, view.size.y, 40.0f);
                                moved.x = std::round(moved.x / xStep) * xStep;
                                moved.y = std::round(moved.y / yStep) * yStep;
                            }
                            if (moved != value.keys[grabbed])
                            {
                                const uint32 movedTo = MoveKey(value, static_cast<uint32>(grabbed), moved);
                                storage->SetInt(ids.grabbedId, static_cast<int>(movedTo));
                                storage->SetInt(ids.selectedId, static_cast<int>(movedTo));
                                highlighted = static_cast<int>(movedTo);
                                edited = true;
                            }
                        }
                        else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f))
                        {
                            Curve::Key& key = value.keys[grabbed];
                            const ImVec2 center = CurveToScreen(view, key.x, key.y);
                            const float slope = ScreenToTangent(view, center, mouse, grabKind == k_GrabInHandle);
                            // 昇格は動かした瞬間に行う。掴んだだけで昇格するとクリックだけでモードが変わるため
                            if (key.mode != Curve::InterpMode::Manual)
                            {
                                const uint32 grabbedIndex = static_cast<uint32>(grabbed);
                                const float inNow = DisplayTangent(value, grabbedIndex, true);
                                const float outNow = DisplayTangent(value, grabbedIndex, false);
                                key.mode = Curve::InterpMode::Manual;
                                key.inTangent = inNow;
                                key.outTangent = outNow;
                                edited = true;
                            }
                            if (grabKind == k_GrabInHandle && slope != key.inTangent)
                            {
                                key.inTangent = slope;
                                edited = true;
                            }
                            if (grabKind == k_GrabOutHandle && slope != key.outTangent)
                            {
                                key.outTangent = slope;
                                edited = true;
                            }
                        }
                    }
                }

                // メニューの対象は開く瞬間に控える。開いている間にマウスが動くと対象の点がずれるため
                if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
                {
                    storage->SetInt(ids.menuPointId, highlighted);
                    const Curve::Key menuAt = ScreenToCurve(view, mouse);
                    storage->SetFloat(ids.menuXId, menuAt.x);
                    storage->SetFloat(ids.menuYId, menuAt.y);
                    // 開く瞬間に activated を立てる。undo の控えを開いている間の編集より前に取らせるため
                    result.activated = true;
                    ImGui::OpenPopup("curve-menu");
                }
                edited |= DrawCurveMenu(value, storage, ids);
                // メニューが開いている間は確定を遅らせる。開いた瞬間に確定するとメニューの編集が履歴に残らないため
                const bool menuOpen = ImGui::IsPopupOpen("curve-menu");
                const bool menuWasOpen = storage->GetBool(ids.menuWasOpenId, false);
                if (!menuOpen && (graphDeactivated || menuWasOpen))
                {
                    result.committed = true;
                }
                storage->SetBool(ids.menuWasOpenId, menuOpen);
                StoreCurveView(*storage, view);

                DrawCurveGraph(
                    *ImGui::GetWindowDrawList(), view, value, highlighted, storage->GetInt(ids.selectedId, -1));
                if (hovered && highlighted >= 0)
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                }
                if (hovered && highlighted >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    const Curve::Key& key = value.keys[highlighted];
                    ImGui::SetTooltip("点 %d\n上下に動かす: 値を変更\n左右に動かす: 位置を変更\nX: %.4f\nY: %.4f",
                                      highlighted + 1, key.x, key.y);
                }
                ImGui::TextDisabled("操作方法 (?)");
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("X: 入力 / Y: 値\nホイール: カーソルを中心にズーム\n"
                                      "Ctrl+ホイール: 横軸のみ / Shift+ホイール: 縦軸のみ\n"
                                      "中ボタン・Alt+左ドラッグ: 表示移動 / F: 全体表示\n"
                                      "点をドラッグ: 移動 / Ctrl: 目盛りに吸着\n"
                                      "ダブルクリック: 点を追加 / 右クリック: メニュー\n"
                                      "数値欄をCtrl+クリック: 直接入力");
                }

                edited |= DrawCurveSelection(value, *storage, ids, result, width);
                edited |= DrawCurveNumbers(value, storage, ids, result);
                result.popupActive |= menuOpen;
                if (result.popupActive)
                {
                    result.committed = false;
                }
                return edited;
            }

            void DrawCurveFieldWidget(void* owner, const FieldDesc& field, FieldWidgetResult& result) noexcept
            {
                Curve value{};
                field.get(owner, &value);
                ImGuiStorage* storage = ImGui::GetStateStorage();
                const ImGuiID wasOpenId = ImGui::GetID("curve-editor-was-open");
                const bool wasOpen = storage->GetBool(wasOpenId, false);
                const float width = ImGui::CalcItemWidth();
                char editorName[64]{};
                ImFormatString(editorName, sizeof(editorName), "カーブ編集###curve-editor-%08X", ImGui::GetID("curve-editor"));
                ImGui::PushItemWidth(width);
                if (ImGui::SmallButton("大きく編集"))
                {
                    result.activated = true;
                    ImGui::OpenPopup(editorName);
                }
                bool edited = false;
                if (!ImGui::IsPopupOpen(editorName))
                {
                    edited = DrawCurveEditor(value, result, width, k_CurveGraphHeight);
                }
                else
                {
                    CurveGraphView preview{};
                    preview.origin = ImGui::GetCursorScreenPos();
                    preview.size = ImVec2{std::max(width, 1.0f), k_CurveGraphHeight};
                    CurveGraphXRange(value, preview.xMin, preview.xMax);
                    ComputeCurveYRange(value, preview.yMin, preview.yMax);
                    DrawCurveGraph(*ImGui::GetWindowDrawList(), preview, value, -1, -1);
                    ImGui::Dummy(preview.size);
                }
                ImGui::PopItemWidth();
                ImGui::SetNextWindowSize(ImVec2{780.0f, 620.0f}, ImGuiCond_FirstUseEver);
                bool keepOpen = true;
                if (ImGui::BeginPopupModal(editorName, &keepOpen, ImGuiWindowFlags_NoSavedSettings))
                {
                    ImGui::TextUnformatted(FieldLabel(field));
                    const float editorWidth = ImGui::GetContentRegionAvail().x;
                    const float editorHeight = std::max(180.0f, ImGui::GetContentRegionAvail().y - 240.0f);
                    edited |= DrawCurveEditor(value, result, editorWidth, editorHeight);
                    ImGui::EndPopup();
                }
                const bool open = ImGui::IsPopupOpen(editorName);
                result.popupActive |= open;
                // 拡大画面のつまみを離すたびに履歴を区切らず、画面を閉じるまでをまとめる
                if (open)
                {
                    result.committed = false;
                }
                else if (wasOpen)
                {
                    result.committed = true;
                }
                storage->SetBool(wasOpenId, open);

                if (edited)
                {
                    // x を左右へドラッグすると並びが崩れるため、編集のたびに並べ直してから書き戻す
                    value.SortKeys();
                    WriteField(owner, field, &value, result);
                }
            }

            // owner の欄 field を、型に合ったウィジェット 1 つで描く。部品と値型の両方が使う
            FieldWidgetResult DrawFieldWidget(void* owner,
                                              const FieldDesc& field,
                                              std::span<const ActorRefOption> refOptions) noexcept
            {
                FieldWidgetResult result;
                // 1 つの欄が部品を幾つも出す。まとめないと始まりと確定を最後の部品からしか拾えない
                ImGui::BeginGroup();
                switch (field.type)
                {
                case FieldType::Float:
                {
                    float value = 0.0f;
                    field.get(owner, &value);
                    if (ImGui::DragFloat("##value", &value, 0.05f))
                    {
                        WriteField(owner, field, &value, result);
                    }
                    break;
                }
                case FieldType::Int:
                {
                    int value = 0;
                    field.get(owner, &value);
                    if (ImGui::DragInt("##value", &value))
                    {
                        WriteField(owner, field, &value, result);
                    }
                    break;
                }
                case FieldType::Bool:
                {
                    bool value = false;
                    field.get(owner, &value);
                    if (ImGui::Checkbox("##value", &value))
                    {
                        WriteField(owner, field, &value, result);
                    }
                    break;
                }
                case FieldType::Vector2:
                {
                    Vector2 value{};
                    field.get(owner, &value);
                    float xy[2] = {value.x, value.y};
                    const AxisEditResult edit = DragAxisFloats(xy, 0.05f);
                    result.activated |= edit.activated;
                    result.committed |= edit.committed;
                    if (edit.changed)
                    {
                        value = Vector2{xy[0], xy[1]};
                        WriteField(owner, field, &value, result);
                    }
                    break;
                }
                case FieldType::Vector3:
                {
                    Vector3 value{};
                    field.get(owner, &value);
                    float xyz[3] = {value.x, value.y, value.z};
                    const AxisEditResult edit = DragAxisFloats(xyz, 0.05f);
                    result.activated |= edit.activated;
                    result.committed |= edit.committed;
                    if (edit.changed)
                    {
                        value = Vector3{xyz[0], xyz[1], xyz[2]};
                        WriteField(owner, field, &value, result);
                    }
                    break;
                }
                case FieldType::Quaternion:
                {
                    // 4 成分を直接触らせると正規化の崩れた回転を作れるので、度の Euler を経由する
                    Quaternion value{};
                    field.get(owner, &value);
                    const Vector3 degrees = QuaternionToEulerDegrees(value);
                    float xyz[3] = {degrees.x, degrees.y, degrees.z};
                    const AxisEditResult edit = DragAxisFloats(xyz, 0.5f, "%.3f°");
                    result.activated |= edit.activated;
                    result.committed |= edit.committed;
                    if (edit.changed)
                    {
                        value = EulerDegreesToQuaternion(Vector3{xyz[0], xyz[1], xyz[2]});
                        WriteField(owner, field, &value, result);
                    }
                    break;
                }
                case FieldType::Color:
                    DrawColorFieldWidget(owner, field, result);
                    break;
                case FieldType::OptionalColor:
                    DrawOptionalColorFieldWidget(owner, field, result);
                    break;
                case FieldType::String:
                case FieldType::Asset:
                {
                    if (field.assetKind != AssetKind::None)
                    {
                        DrawAssetFieldWidget(owner, field, result);
                    }
                    else
                    {
                        DrawStringFieldWidget(owner, field, result);
                    }
                    break;
                }
                case FieldType::ActorRef:
                    DrawActorRefFieldWidget(owner, field, refOptions, result);
                    break;
                case FieldType::Curve:
                    DrawCurveFieldWidget(owner, field, result);
                    break;
                }
                ImGui::EndGroup();

                result.activated |= ImGui::IsItemActivated();
                // 編集無しのクリックでもラッチを解くため、確定ではなく非活性化で committed を立てる
                // 空編集は CommitClassObjEdit が before==after で弾くので履歴は汚れない
                if (!result.popupActive)
                {
                    result.committed |= ImGui::IsItemDeactivated();
                }
                WarnRejectedWriteOnCommit(field, result);
                return result;
            }

            float MinimumFieldValueWidth(const ReflectionInfo& info, const char* filter = nullptr) noexcept
            {
                float width = 0.0f;
                for (std::size_t i = 0; i < info.fieldCount; ++i)
                {
                    const FieldDesc& field = info.fields[i];
                    if (filter != nullptr && !NameMatches(FieldLabel(field), filter))
                    {
                        continue;
                    }
                    if (field.type == FieldType::Quaternion)
                    {
                        width = std::max(width, MinimumAxisFieldsWidth(3, "%.3f°"));
                    }
                    else if (field.type == FieldType::Vector3)
                    {
                        width = std::max(width, MinimumAxisFieldsWidth(3));
                    }
                    else if (field.type == FieldType::Vector2)
                    {
                        width = std::max(width, MinimumAxisFieldsWidth(2));
                    }
                }
                if (width == 0.0f)
                {
                    return 0.0f;
                }
                return width + RevertButtonWidth() + ImGui::GetStyle().ItemSpacing.x +
                       ImGui::GetStyle().CellPadding.x * 2.0f;
            }

            // 見出し groupIndex が束ねる欄の終わり (含まない添字)。次の見出しの始まりか、欄の数
            std::size_t GroupEnd(const ReflectionInfo& info, std::size_t groupIndex) noexcept
            {
                if (groupIndex + 1 < info.groupCount)
                {
                    return info.groups[groupIndex + 1].firstField;
                }
                return info.fieldCount;
            }

            // 見出しの行を出し、下の欄を出すかを返す。検索中は畳めない行にして、一致した欄が無ければ行ごと出さない
            // 検索で畳んだ状態を書き換えると、検索を消した時に開け閉めが崩れる
            bool DrawGroupRow(const ReflectionInfo& info,
                              std::size_t groupIndex,
                              const char* filter,
                              bool filtering) noexcept
            {
                const FieldGroup& group = info.groups[groupIndex];
                const std::size_t end = GroupEnd(info, groupIndex);
                if (filtering)
                {
                    const bool anyMatch =
                        std::any_of(info.fields + group.firstField,
                                    info.fields + end,
                                    [filter](const FieldDesc& field) { return NameMatches(FieldLabel(field), filter); });
                    if (!anyMatch)
                    {
                        return false;
                    }
                }
                ImGui::TableNextRow();
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
                ImGui::TableSetColumnIndex(0);
                if (filtering)
                {
                    ImGui::TextDisabled("%s", group.name);
                    return true;
                }
                const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAllColumns |
                                                 ImGuiTreeNodeFlags_NoTreePushOnOpen;
                return ImGui::TreeNodeEx(group.name, flags, "%s (%zu)", group.name, end - group.firstField);
            }

            // 欄を宣言の並びで回し、見出しの始まる所で見出しの行を挟む。畳んだ見出しの欄と、検索に外れた欄は飛ばす
            // 欄を持たない見出しは出さない。drawRow は出す欄の添字を受け取って 1 行を描く
            template <class DrawRow>
            void DrawGroupedRows(const ReflectionInfo& info, const char* filter, DrawRow&& drawRow) noexcept
            {
                const bool filtering = filter != nullptr && filter[0] != '\0';
                std::size_t nextGroup = 0;
                bool groupOpen = true;
                for (std::size_t i = 0; i < info.fieldCount; ++i)
                {
                    while (nextGroup < info.groupCount && info.groups[nextGroup].firstField <= i)
                    {
                        const std::size_t groupIndex = nextGroup;
                        ++nextGroup;
                        if (info.groups[groupIndex].firstField < GroupEnd(info, groupIndex))
                        {
                            groupOpen = DrawGroupRow(info, groupIndex, filter, filtering);
                        }
                    }
                    if (!groupOpen)
                    {
                        continue;
                    }
                    if (filtering && !NameMatches(FieldLabel(info.fields[i]), filter))
                    {
                        continue;
                    }
                    drawRow(i);
                }
            }
        } // namespace

        ClassObjEditResult DrawReflectedClassObj(Object* comp,
                                                 std::span<const ActorRefOption> refOptions,
                                                 const char* partKey,
                                                 const char* filter,
                                                 const SceneActor* actor,
                                                 const EditorAppearance* appearance) noexcept
        {
            NS_ASSERT(App, comp != nullptr, "DrawReflectedClassObj: 部品が null");
            const ReflectionInfo* info = comp->GetReflection();
            if (info == nullptr || info->fieldCount == 0)
            {
                return ClassObjEditResult{};
            }
            if (!BeginFieldTable("##fields", MinimumFieldValueWidth(*info, filter)))
            {
                return ClassObjEditResult{};
            }

            ClassObjEditResult result;
            const auto drawRow = [&](std::size_t i) {
                const FieldDesc& field = info->fields[i];
                const bool changed = ShowsOverride(comp, partKey, field, actor);
                const bool wasChanged = result.changed;
                ImU32 background = 0;
                if (actor != nullptr && appearance != nullptr)
                {
                    if (const std::optional<Color> color = FieldRowColor(*appearance, actor, field))
                    {
                        background = ImGui::ColorConvertFloat4ToU32(ImVec4{color->x, color->y, color->z, color->w});
                    }
                }
                ImGui::PushID(static_cast<int>(i));
                FieldRow(FieldLabel(field), changed, background);

                const FieldWidgetResult widget = DrawFieldWidget(comp, field, refOptions);
                result.changed |= widget.changed;
                result.activated |= widget.activated;
                result.committed |= widget.committed;

                if (result.changed && !wasChanged)
                {
                    result.changedTarget = comp;
                    result.changedField = &field;
                }

                const OverrideAction action = OverrideButton(changed);
                if (action == OverrideAction::Revert)
                {
                    result.revertTarget = comp;
                    result.revertField = &field;
                }
                ImGui::PopID();
            };
            DrawGroupedRows(*info, filter, drawRow);
            EndFieldTable();
            return result;
        }

        ValueEditResult DrawReflectedValue(void* value,
                                           const ReflectionInfo& info,
                                           std::span<const ActorRefOption> refOptions) noexcept
        {
            if (value == nullptr || info.fieldCount == 0)
            {
                return ValueEditResult{};
            }
            if (!BeginFieldTable("##value-fields", MinimumFieldValueWidth(info)))
            {
                return ValueEditResult{};
            }
            ValueEditResult result;
            const auto drawRow = [&](std::size_t i) {
                const FieldDesc& field = info.fields[i];
                ImGui::PushID(static_cast<int>(i));
                FieldRow(FieldLabel(field));
                const FieldWidgetResult widget = DrawFieldWidget(value, field, refOptions);
                result.activated |= widget.activated;
                result.committed |= widget.committed;
                if (widget.changed && !result.changed)
                {
                    result.changedField = &field;
                }
                result.changed |= widget.changed;
                // 部品の行と値の幅を揃えるため、上書きボタンの幅だけ空ける
                (void)OverrideButton(false);
                ImGui::PopID();
            };
            DrawGroupedRows(info, nullptr, drawRow);
            EndFieldTable();
            return result;
        }

#else
        ClassObjEditResult DrawReflectedClassObj(Object*,
                                                 std::span<const ActorRefOption>,
                                                 const char*,
                                                 const char*,
                                                 const SceneActor*,
                                                 const EditorAppearance*) noexcept
        {
            return ClassObjEditResult{};
        }

        ValueEditResult DrawReflectedValue(void*, const ReflectionInfo&, std::span<const ActorRefOption>) noexcept
        {
            return ValueEditResult{};
        }
#endif
    } // namespace Editor
} // namespace NS
