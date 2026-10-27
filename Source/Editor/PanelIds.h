#pragma once

namespace NS::Editor
{
        //! 表示名を変えるパネルは 表示名###ID の形式を使い、### 以降の識別子を固定する
        inline constexpr const char* k_PanelScene = "Scene";
        inline constexpr const char* k_PanelGame = "Game";
        // 保存済みのドック配置を保つため、### 以降の識別子は従来の値を使う
        inline constexpr const char* k_PanelActorList = "アクター一覧###Hierarchy";
        inline constexpr const char* k_PanelInspector = "インスペクター###Inspector";
        inline constexpr const char* k_PanelConsole = "Console";
        inline constexpr const char* k_PanelAssets = "Assets";
        inline constexpr const char* k_PanelEditMode = "Edit Mode";
        inline constexpr const char* k_PanelActionTimeline = "動作のタイムライン###ActionTimeline";
} // namespace NS::Editor
