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

    //! 前面の映像を映すタブを返す。編集中は Scene、プレイ中は Game
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

    //! @brief 入力の持ち主を決める材料。フレームの終わりに集め、次のフレームのメッセージの振り分けに効く
    struct InputOwnerQuery
    {
        bool playMode = false;                 // プレイモード中か
        bool uiWantsMouse = false;             // ImGui がマウスを欲しがっているか
        bool uiWantsKeyboard = false;          // ImGui がキーボードを欲しがっているか。項目を押している間も立つ
        bool textInput = false;                // 文字の入力欄に居るか
        bool editSceneLatched = false;         // 編集中の Scene の画像のラッチ
        bool gameLatched = false;              // プレイ中の Game の画像のラッチ
        bool cursorReleased = false;           // プレイ中にカーソルを出して固定を解いているか
        bool sceneLatched = false;             // プレイ中の Scene の画像 (自由視点) のラッチ
        bool sceneLooking = false;             // Scene の右ドラッグで見回している最中か
        std::optional<CenterTab> focusedPanel; // 焦点のある中央パネル。他の窓に焦点があれば空
    };

    //! @brief UI とゲームの入力の取り分
    struct InputOwnership
    {
        bool uiMouse = false;          // UI がマウスを持つか
        bool uiKeyboard = false;       // UI がキーボードを持つか
        bool leftButtonToGame = false; // UI がマウスを持つ間も、左ボタンだけゲームへ渡すか
    };

    //! @brief UI とゲームの入力の取り分を決める
    //! @details プレイ中は Scene の画像の上でも左ボタンを溜めへ渡し、焦点が中央パネルならキーボードも渡す
    [[nodiscard]] InputOwnership ResolveInputOwnership(InputOwnerQuery query) noexcept;

    //! @brief プレイ中のカーソルの置き方
    enum class PlayCursor : std::uint8_t
    {
        Captured, // 消して Game の画像の中心へ固定し、マウスを相対にする。視点を回せる
        Released  // 出して固定と相対を解く。タブやパネルを押せる
    };

    //! @brief 一時停止を切り替えた後のカーソルを決める材料
    struct PauseToggleQuery
    {
        bool paused = false;          // 切り替えた後に止まっているか
        bool gameViewInFront = false; // Game のパネルが裏へ隠れずに映っているか
    };

    //! 一時停止を切り替えた後のカーソルを返す。再開は Game が映っている時だけ固定へ戻す
    [[nodiscard]] PlayCursor CursorAfterPauseToggle(PauseToggleQuery query) noexcept;

    //! @brief カーソルを固定へ戻すかを決める材料
    struct RecaptureQuery
    {
        bool playMode = false;         // プレイモード中か
        bool paused = false;           // 一時停止中か
        bool cursorReleased = false;   // カーソルを出して固定を解いているか
        bool gameImageClicked = false; // このフレームに Game の画像を左クリックしたか
    };

    //! 出したカーソルを固定へ戻す場合 true、それ以外の場合は false。止めている間は戻さない
    [[nodiscard]] bool ShouldRecaptureCursor(RecaptureQuery query) noexcept;
} // namespace NS::Editor
