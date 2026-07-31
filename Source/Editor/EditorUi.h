#pragma once

#if NS_EDITOR_ENABLED

#include <imgui.h>

#include <cctype>

namespace NS::Editor
{
    // 状態メッセージの表示色。 失敗は赤、 成功は緑
    inline const ImVec4 k_MsgErrorColor{1.0f, 0.4f, 0.4f, 1.0f};
    inline const ImVec4 k_MsgOkColor{0.4f, 1.0f, 0.4f, 1.0f};

    // コンポーネントのヘッダは中身より一段明るくして、 どこからどこまでが 1 個か見えるようにする
    inline const ImVec4 k_ComponentHeaderColor{0.26f, 0.29f, 0.34f, 1.0f};
    inline const ImVec4 k_ComponentHeaderHoveredColor{0.33f, 0.37f, 0.43f, 1.0f};
    inline const ImVec4 k_ComponentHeaderActiveColor{0.38f, 0.43f, 0.50f, 1.0f};

    // 欄の名前列が占める幅の割合。 残りが値列
    inline constexpr float k_FieldNameColumnRatio = 0.42f;

    // 名前 / 値の 2 列で欄を並べ始める。 名前が左端に縦に揃うと目が滑らない
    inline bool BeginFieldTable(const char* id) noexcept
    {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
            return false;
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch, k_FieldNameColumnRatio);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 1.0f - k_FieldNameColumnRatio);
        return true;
    }

    inline void EndFieldTable() noexcept
    {
        ImGui::EndTable();
    }

    // 値列の右端に置く戻すボタンの幅
    inline float RevertButtonWidth() noexcept
    {
        return ImGui::CalcTextSize("戻す").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    }

    // 1 行の名前を左列へ書いて値列へ移り、 値のウィジェットに残り幅を割り当てる
    inline void FieldRow(const char* label) noexcept
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-(RevertButtonWidth() + ImGui::GetStyle().ItemSpacing.x));
    }

    // 既定と違う行だけ右端に戻すボタンを出す。 同じ値の行は幅を空けるだけで並びを保つ
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

    // 検索欄の一致判定。 大小文字を無視して部分一致を見る
    inline bool NameMatches(const char* name, const char* filter) noexcept
    {
        if (name == nullptr || filter == nullptr || filter[0] == '\0')
            return true;
        for (const char* start = name; *start != '\0'; ++start)
        {
            const char* a = start;
            const char* b = filter;
            while (*b != '\0' &&
                   std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b)))
            {
                ++a;
                ++b;
            }
            if (*b == '\0')
                return true;
        }
        return false;
    }
} // namespace NS::Editor

#endif
