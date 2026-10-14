#pragma once

namespace NS::Editor
{
    //! 表示名を変えるパネルは 表示名###ID の形式を使い、### 以降の識別子を固定する
    inline constexpr const char* k_PanelScene = "Scene";
    inline constexpr const char* k_PanelGame = "Game";
    inline constexpr const char* k_PanelHierarchy = "ヒエラルキー###Hierarchy";
    inline constexpr const char* k_PanelInspector = "インスペクター###Inspector";
    inline constexpr const char* k_PanelConsole = "Console";
    inline constexpr const char* k_PanelAssets = "Assets";
    inline constexpr const char* k_PanelEditMode = "Edit Mode";
    inline constexpr const char* k_PanelHitTimeline = "当たりのタイムライン###HitTimeline";
} // namespace NS::Editor
