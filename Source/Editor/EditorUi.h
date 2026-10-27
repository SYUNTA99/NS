#pragma once

#if NS_EDITOR_ENABLED

#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>
#include <span>

namespace NS
{
    namespace Editor
    {
        // 状態メッセージの表示色。失敗は赤、警告は橙、成功は緑
        inline const ImVec4 k_MsgErrorColor{1.0f, 0.4f, 0.4f, 1.0f};
        inline const ImVec4 k_MsgWarnColor{1.0f, 0.78f, 0.35f, 1.0f};
        inline const ImVec4 k_MsgOkColor{0.4f, 1.0f, 0.4f, 1.0f};

        // アクターの持ち物のヘッダは中身より一段明るくして、どこからどこまでが 1 個か見えるようにする
        inline const ImVec4 k_ClassObjHeaderColor{0.26f, 0.29f, 0.34f, 1.0f};
        inline const ImVec4 k_ClassObjHeaderHoveredColor{0.33f, 0.37f, 0.43f, 1.0f};
        inline const ImVec4 k_ClassObjHeaderActiveColor{0.38f, 0.43f, 0.50f, 1.0f};

        // 欄の名前列が占める幅の割合。残りが値列
        inline constexpr float k_FieldNameColumnRatio = 0.42f;

        // 欄の行の地の色の上下の端から、欄の中身までの余白 (ピクセル)
        inline constexpr float k_FieldRowPaddingY = 4.5f;
        // 欄の行の上端に残す、塗らない隙間 (ピクセル)。続けて塗った行が 1 枚の帯に見えないようにする
        // 中身は全部の行でこの分だけ下げ、地の色の上下の余白を揃えて文字を地の色の真ん中に置く
        inline constexpr float k_FieldRowGap = 5.0f;

        // 今の列の中身を、行の上端の隙間の分だけ下げる
        inline void SkipFieldRowGap() noexcept
        {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + k_FieldRowGap);
        }

        inline bool BeginFieldTable(const char* id, float minimumValueWidth = 0.0f) noexcept
        {
            int columns = 2;
            if (minimumValueWidth > ImGui::GetContentRegionAvail().x * (1.0f - k_FieldNameColumnRatio))
            {
                columns = 1;
            }
            // 表は始めた時の余白を覚えるので、BeginTable の間だけ変える
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                                ImVec2{ImGui::GetStyle().CellPadding.x, k_FieldRowPaddingY});
            const bool begun =
                ImGui::BeginTable(id, columns, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp);
            ImGui::PopStyleVar();
            if (!begun)
            {
                return false;
            }
            if (columns == 2)
            {
                ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch, k_FieldNameColumnRatio);
                ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 1.0f - k_FieldNameColumnRatio);
            }
            else
            {
                ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
            }
            return true;
        }

        inline void EndFieldTable() noexcept
        {
            ImGui::EndTable();
        }

        // アクターの値が .actor と違う欄の名前の色。値を変えた欄が一覧の中で目に留まる
        inline const ImVec4 k_OverriddenFieldColor{1.0f, 0.78f, 0.35f, 1.0f};

        // 値列の右端に置くボタンの幅。戻すボタンと上書きボタンの広い方に揃え、どの行も値の幅を揃える
        inline float RevertButtonWidth() noexcept
        {
            return ImGui::CalcTextSize("上書き").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        }

        // 今の表の今の列の、行の上端の k_FieldRowGap をパネルの地の色で塗り直す
        // 行の地は表が行の終わりに中身の下へ描くので、中身と同じ層に描けば地の上に重なる
        inline void DrawFieldRowGap() noexcept
        {
            const ImGuiTable* table = ImGui::GetCurrentTable();
            if (table == nullptr)
            {
                return;
            }
            const ImRect cell = ImGui::TableGetCellBgRect(table, table->CurrentColumn);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2{cell.Min.x, table->RowPosY1},
                                                      ImVec2{cell.Max.x, table->RowPosY1 + k_FieldRowGap},
                                                      ImGui::GetColorU32(ImGuiCol_WindowBg));
        }

        // overridden の行は名前に色を付け、この 1 体の値が .actor と違うと分かるようにする
        // background が 0 でなければ行の地をその色で塗り、上端に隙間を残す
        inline void FieldRow(const char* label, bool overridden = false, ImU32 background = 0) noexcept
        {
            ImGui::TableNextRow();
            if (background != 0)
            {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, background);
            }
            ImGui::TableSetColumnIndex(0);
            if (background != 0)
            {
                DrawFieldRowGap();
            }
            SkipFieldRowGap();
            ImGui::AlignTextToFramePadding();
            const bool stacked = ImGui::TableGetColumnCount() == 1;
            if (stacked)
            {
                ImGui::PushTextWrapPos(0.0f);
            }
            if (overridden)
            {
                ImGui::TextColored(k_OverriddenFieldColor, "%s", label);
            }
            else
            {
                ImGui::TextUnformatted(label);
            }
            if (stacked)
            {
                ImGui::PopTextWrapPos();
            }
            else
            {
                ImGui::TableSetColumnIndex(1);
                if (background != 0)
                {
                    DrawFieldRowGap();
                }
                SkipFieldRowGap();
            }
            ImGui::SetNextItemWidth(-(RevertButtonWidth() + ImGui::GetStyle().ItemSpacing.x));
        }

        struct AxisEditResult
        {
            bool changed = false;
            bool activated = false;
            bool committed = false;
        };

        inline float MinimumAxisFieldsWidth(int count, const char* format = "%.3f") noexcept
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            char number[64]{};
            ImFormatString(number, sizeof(number), format, -100.0);
            const float numberWidth = ImGui::CalcTextSize(number).x + style.FramePadding.x * 2.0f;
            return (numberWidth + ImGui::CalcTextSize("X").x) * static_cast<float>(count) +
                   style.ItemInnerSpacing.x * static_cast<float>(count * 2 - 1);
        }

        //! 2〜3成分を軸名付きで編集する。幅が足りない時は軸ごとに行を分ける
        //! 各軸の編集開始と終了を返し、呼び出し側で取り消しの履歴をまとめられる
        inline AxisEditResult DragAxisFloats(std::span<float> values, float speed, const char* format = "%.3f") noexcept
        {
            IM_ASSERT(values.size() >= 2 && values.size() <= 3);
            const char* axes[] = {"X", "Y", "Z"};
            const ImVec4 colors[] = {{1.0f, 0.45f, 0.45f, 1.0f}, {0.5f, 0.9f, 0.5f, 1.0f}, {0.45f, 0.7f, 1.0f, 1.0f}};
            const int count = static_cast<int>(values.size());
            const float width = ImGui::CalcItemWidth();
            const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
            const float labelWidth = ImGui::CalcTextSize("X").x;
            const bool horizontal = width >= MinimumAxisFieldsWidth(count, format);
            float inputWidth = width - labelWidth - spacing;
            if (horizontal)
            {
                inputWidth =
                    (width - labelWidth * static_cast<float>(count) - spacing * static_cast<float>(count * 2 - 1)) /
                    static_cast<float>(count);
            }
            AxisEditResult result;
            ImGui::BeginGroup();
            for (int axis = 0; axis < count; ++axis)
            {
                if (axis > 0 && horizontal)
                {
                    ImGui::SameLine(0.0f, spacing);
                }
                ImGui::PushID(axes[axis]);
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(colors[axis], "%s", axes[axis]);
                ImGui::SameLine(0.0f, spacing);
                ImGui::SetNextItemWidth(std::max(inputWidth, 1.0f));
                result.changed |= ImGui::DragFloat("##value", &values[axis], speed, 0.0f, 0.0f, format);
                result.activated |= ImGui::IsItemActivated();
                result.committed |= ImGui::IsItemDeactivated();
                ImGui::PopID();
            }
            ImGui::EndGroup();
            return result;
        }

        // 既定と違う行だけ右端に戻すボタンを出す。同じ値の行は幅を空けるだけで並びを保つ
        inline bool RevertButton(bool changed) noexcept
        {
            ImGui::SameLine();
            if (!changed)
            {
                ImGui::Dummy(ImVec2{RevertButtonWidth(), 0.0f});
                return false;
            }
            return ImGui::SmallButton("戻す");
        }

        // 上書きボタンで選ばれた操作
        enum class OverrideAction
        {
            None,
            Revert, // 既定に戻す。.actor の値へ戻して上書きをやめる
        };

        // .actor の値と違う行だけ右端に上書きボタンを出し、押すと戻すかを選ばせる
        // 同じ値の行は幅を空けるだけで並びを保つ
        inline OverrideAction OverrideButton(bool overridden) noexcept
        {
            ImGui::SameLine();
            if (!overridden)
            {
                ImGui::Dummy(ImVec2{RevertButtonWidth(), 0.0f});
                return OverrideAction::None;
            }
            if (ImGui::SmallButton("上書き"))
            {
                ImGui::OpenPopup("##override");
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(".actor と違う値");
            }
            OverrideAction action = OverrideAction::None;
            if (ImGui::BeginPopup("##override"))
            {
                if (ImGui::MenuItem("既定に戻す"))
                {
                    action = OverrideAction::Revert;
                }
                ImGui::EndPopup();
            }
            return action;
        }

        // 検索欄の一致判定。大小文字を無視して部分一致を見る
        inline bool NameMatches(const char* name, const char* filter) noexcept
        {
            if (name == nullptr || filter == nullptr || filter[0] == '\0')
            {
                return true;
            }
            return ImStristr(name, nullptr, filter, nullptr) != nullptr;
        }
    } // namespace Editor
} // namespace NS

#endif
