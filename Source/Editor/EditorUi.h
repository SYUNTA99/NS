#pragma once

#if NS_EDITOR_ENABLED

#include <imgui.h>
#include <imgui_internal.h>

namespace NS::Editor
{
    // 状態メッセージの表示色。失敗は赤、成功は緑
    inline const ImVec4 k_MsgErrorColor{1.0f, 0.4f, 0.4f, 1.0f};
    inline const ImVec4 k_MsgOkColor{0.4f, 1.0f, 0.4f, 1.0f};

    // コンポーネントのヘッダは中身より一段明るくして、どこからどこまでが 1 個か見えるようにする
    inline const ImVec4 k_ComponentHeaderColor{0.26f, 0.29f, 0.34f, 1.0f};
    inline const ImVec4 k_ComponentHeaderHoveredColor{0.33f, 0.37f, 0.43f, 1.0f};
    inline const ImVec4 k_ComponentHeaderActiveColor{0.38f, 0.43f, 0.50f, 1.0f};

    // 欄の名前列が占める幅の割合。残りが値列
    inline constexpr float k_FieldNameColumnRatio = 0.42f;

    // 名前 / 値の 2 列で欄を並べ始める。名前が左端に縦に揃うと目が滑らない
    inline bool BeginFieldTable(const char* id) noexcept
    {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
        {
            return false;
        }
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch, k_FieldNameColumnRatio);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 1.0f - k_FieldNameColumnRatio);
        return true;
    }

    inline void EndFieldTable() noexcept
    {
        ImGui::EndTable();
    }

    // 種類の既定値を上書きしている欄の名前の色。値を変えた欄が一覧の中で目に留まる
    inline const ImVec4 k_OverriddenFieldColor{1.0f, 0.78f, 0.35f, 1.0f};

    // 値列の右端に置くボタンの幅。戻すボタンと上書きボタンの広い方に揃え、どの行も値の幅を揃える
    inline float RevertButtonWidth() noexcept
    {
        return ImGui::CalcTextSize("上書き").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    }

    // 1 行の名前を左列へ書いて値列へ移り、値のウィジェットに残り幅を割り当てる
    // overridden の行は名前に色を付け、種類の既定値を上書きしていると分かるようにする
    inline void FieldRow(const char* label, bool overridden = false) noexcept
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        if (overridden)
        {
            ImGui::TextColored(k_OverriddenFieldColor, "%s", label);
        }
        else
        {
            ImGui::TextUnformatted(label);
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-(RevertButtonWidth() + ImGui::GetStyle().ItemSpacing.x));
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

    // 上書きの印のボタンで選ばれた操作
    enum class OverrideAction
    {
        None,
        Revert,  // 既定に戻す。種類の既定値へ戻して上書きをやめる
        Promote, // 種類の既定にする。今の値を種類の既定値へ書き、同じ種類の全ての個体へ広げる
    };

    // 種類の既定値と違う行だけ右端に上書きの印を出し、押すと戻すか種類の既定にするかを選ばせる
    // 同じ値の行は幅を空けるだけで並びを保つ。promotable が偽の欄 (参照など個体の物) は種類の既定にできない
    inline OverrideAction OverrideButton(bool overridden, bool promotable) noexcept
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
            ImGui::SetTooltip("種類の既定値と違う値");
        }
        OverrideAction action = OverrideAction::None;
        if (ImGui::BeginPopup("##override"))
        {
            if (ImGui::MenuItem("既定に戻す"))
            {
                action = OverrideAction::Revert;
            }
            if (ImGui::MenuItem("種類の既定にする", nullptr, false, promotable))
            {
                action = OverrideAction::Promote;
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
} // namespace NS::Editor

#endif
