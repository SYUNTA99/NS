#pragma once

#include <cstdint>
#include <optional>

//! @brief プレイ制御ツールバーと中央タブ (Scene / Game) の表示状態を決める純関数群

namespace NS::Editor
{
    //! 中央ノードのタブ
    enum class CenterTab : std::uint8_t
    {
        Scene,
        Game
    };

    //! 生きた映像を映すタブを返す。編集中は Scene、プレイ中は Game
    [[nodiscard]] CenterTab LiveCenterTab(bool playMode) noexcept;

    //! @brief ツールバー状態の入力。現在のモードと一時停止フラグの写し
    struct PlayModeSnapshot
    {
        bool playMode = false; // プレイモード中か
        bool paused = false;   // 一時停止中か
    };

    //! @brief ツールバー 3 ボタンの表示状態
    struct PlayToolbarState
    {
        bool playActive = false;   // 再生中か。開始/停止トグルの ▶/■ 切替と押下ハイライトに使う
        bool pauseDown = false;    // 一時停止ボタンを押下表示にするか
        bool pauseEnabled = false; // 一時停止ボタンを操作できるか
        bool stepEnabled = false;  // コマ送りボタンを操作できるか
    };

    //! ツールバー 3 ボタンの表示状態を決める。一時停止とコマ送りはプレイ中だけ操作できる
    [[nodiscard]] PlayToolbarState MakePlayToolbarState(PlayModeSnapshot snapshot) noexcept;

    //! @brief タブ自動フォーカスの切替検知に使う、前フレームと今フレームのモードの組
    struct ModeTransition
    {
        bool wasPlayMode = false; // 前フレームがプレイモードだったか
        bool playMode = false;    // 今フレームがプレイモードか
    };

    //! モードが変わったフレームだけフォーカス先のタブを返す。変化が無ければ空
    [[nodiscard]] std::optional<CenterTab> TabFocusOnModeChange(ModeTransition transition) noexcept;

    //! @brief 中央パネル 1 枚の役割
    enum class CenterPanelRole : std::uint8_t
    {
        Placeholder, // 説明文だけ
        LiveView,    // 生きた映像。マウスはゲームへ通す
        FreeView     // 自由カメラ映像。マウスは UI が持ち自由カメラが使う
    };

    //! @brief 役割判定の入力。プレイ中は Game パネルを先に描き、その結果を渡す
    struct CenterPanelQuery
    {
        CenterTab tab = CenterTab::Scene; // 判定するタブ
        bool playMode = false;            // プレイモード中か
        bool otherDisplayed = false;      // もう片方のタブが先に映像を出したか
    };

    //! 中央パネルの役割を決める。プレイ中の Scene は Game が裏のときだけ自由視点を映す
    [[nodiscard]] CenterPanelRole ResolveCenterPanelRole(CenterPanelQuery query) noexcept;
} // namespace NS::Editor
