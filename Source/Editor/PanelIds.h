#pragma once

namespace NS::Editor
{
    //! ドックパネルのウィンドウ名。 表示だけ日本語にしたいものは "表示名###ID" 形式にし、 ### 以降の ID
    //! を英語で固定する ImGui は ### 以降だけを識別子ハッシュに使うので、 Begin / Dock / Focus / Find が同じ ID
    //! で一致する
    inline constexpr const char* k_PanelScene = "Scene";
    inline constexpr const char* k_PanelGame = "Game";
    inline constexpr const char* k_PanelHierarchy = "ヒエラルキー###Hierarchy";
    inline constexpr const char* k_PanelInspector = "インスペクター###Inspector";
    inline constexpr const char* k_PanelConsole = "Console";
    inline constexpr const char* k_PanelAssets = "Assets";
    inline constexpr const char* k_PanelEditMode = "Edit Mode";
} // namespace NS::Editor
