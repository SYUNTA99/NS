#pragma once

#include "NSlib/Core/Types.h"
#include <optional>

/// プレイ制御ツールバーと中央タブ (Scene / Game) の表示状態を決める純関数群

namespace NS
{
    namespace Editor
    {
        /// 中央ノードのタブ
        enum class CenterTab : uint8
        {
            Scene,
            Game
        };

        /// ツールバー状態の入力。現在のモードと一時停止フラグの写し
        struct PlayModeSnapshot
        {
            bool playMode = false;
            bool paused = false;
        };

        /// ツールバー 3 ボタンの表示状態
        struct PlayToolbarState
        {
            bool playActive = false;   // 再生中か。開始/停止トグルの ▶/■ 切替と押下ハイライトに使う
            bool pauseDown = false;    // 一時停止ボタンを押下表示にするか
            bool pauseEnabled = false; // 一時停止ボタンを操作できるか
            bool stepEnabled = false;  // コマ送りボタンを操作できるか
        };

        /// ツールバー 3 ボタンの表示状態を決める。一時停止とコマ送りはプレイ中だけ操作できる
        [[nodiscard]] PlayToolbarState MakePlayToolbarState(PlayModeSnapshot snapshot) noexcept;

        /// タブ自動フォーカスの切替検知に使う、前フレームと今フレームのモードの組
        struct ModeTransition
        {
            bool wasPlayMode = false;
            bool playMode = false;
        };

        /// モードが変わったフレームだけフォーカス先のタブを返す。変化が無ければ空
        [[nodiscard]] std::optional<CenterTab> TabFocusOnModeChange(ModeTransition transition) noexcept;

        /// 入力の持ち主を決める材料。フレームの終わりに集め、次のフレームのメッセージの振り分けに効く
        struct InputOwnerQuery
        {
            bool playMode = false;
            bool uiWantsMouse = false;             // ImGui がマウスを欲しがっているか
            bool uiWantsKeyboard = false;          // ImGui がキーボードを欲しがっているか。項目を押している間も立つ
            bool textInput = false;                // 文字の入力欄に居るか
            bool itemActive = false;               // ImGui の項目を押したまま操作しているか
            bool modalOpen = false;                // 曲線の編集などのモーダルの窓を開いているか
            bool editSceneLatched = false;         // 編集中の Scene の画像のラッチ
            bool gameLatched = false;              // プレイ中の Game の画像のラッチ
            bool cursorReleased = false;           // プレイ中にカーソルを出して固定を解いているか
            bool sceneLatched = false;             // プレイ中の Scene の画像 (自由視点) のラッチ
            bool sceneLooking = false;             // Scene の右ドラッグで見回している最中か
            std::optional<CenterTab> focusedPanel; // 焦点のある中央パネル。他の窓に焦点があれば空
        };

        /// UI とゲームの入力の取り分
        struct InputOwnership
        {
            bool uiMouse = false;
            bool uiKeyboard = false;
            bool leftButtonToGame = false; // UI がマウスを持つ間も、左ボタンだけゲームへ渡すか
            // 編集のショートカット (削除・複製・寄る・元に戻す など) を止めるか。
            // パネルに焦点があるだけでは止めず、文字を打つ間・項目を操作する間・モーダルの窓を開いている間だけ止める
            bool editShortcutsBlocked = false;
        };

        /// UI とゲームの入力の取り分を決める
        /// プレイ中は Scene の画像の上でも左ボタンを溜めへ渡し、焦点が中央パネルならキーボードも渡す
        [[nodiscard]] InputOwnership ResolveInputOwnership(InputOwnerQuery query) noexcept;

        /// プレイ中のカーソルの置き方
        enum class PlayCursor : uint8
        {
            Captured, // 消して Game の画像の中心へ固定し、マウスを相対にする。視点を回せる
            Released  // 出して固定と相対を解く。タブやパネルを押せる
        };

        /// 一時停止を切り替えた後のカーソルを決める材料
        struct PauseToggleQuery
        {
            bool paused = false;          // 切り替えた後に止まっているか
            bool gameViewInFront = false; // Game のパネルが裏へ隠れずに映っているか
        };

        /// 一時停止を切り替えた後のカーソルを返す。再開は Game が映っている時だけ固定へ戻す
        [[nodiscard]] PlayCursor CursorAfterPauseToggle(PauseToggleQuery query) noexcept;

        /// カーソルを固定へ戻すかを決める材料
        struct RecaptureQuery
        {
            bool playMode = false;
            bool paused = false;
            bool cursorReleased = false;   // カーソルを出して固定を解いているか
            bool gameViewClicked = false; // このフレームに Game の画像か見出しを左クリックしたか
        };

        /// 出したカーソルを固定へ戻す場合 true、それ以外の場合は false。止めている間は戻さない
        [[nodiscard]] bool ShouldRecaptureCursor(RecaptureQuery query) noexcept;
    } // namespace Editor
} // namespace NS
