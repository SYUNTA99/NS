#pragma once

#include "Editor/ViewportSurface.h"
#include "NSlib/Core/NonCopyable.h"

struct ImVec2;

class LevelEditorController;

namespace NS
{
    namespace Editor
    {
        //! 編集視点を映すパネル。編集時は配置操作、プレイ時は自由視点の入力を受け付ける
        class SceneViewPanel : public NonCopyable
        {
        public:
            //! 描画先の初期目標サイズを窓サイズに合わせる
            void SetInitialSize(const Size2D& size) noexcept { m_surface.SetInitialSize(size); }

            //! フレーム先頭で可視状態を false にする。描画パス (通常 / 全面化) が立て直す
            void ResetVisibility() noexcept { m_surface.ResetVisibility(); }

            //! Scene パネルを 1 枚描く。編集中は入力矩形、プレイ中は自由視点入力を controller へ渡す
            void Render(LevelEditorController& editor) noexcept;

            //! 中央以外を全面化する間、前面の矩形とカーソルの重なりを無効化する
            void Suppress(LevelEditorController& editor) noexcept;

            //! F5 全画面直描き中は中央矩形と自由視点の上書きを外す
            void ClearForHiddenUi(LevelEditorController& editor) noexcept;

            //! 可視なら視点と編集用の図形を含むビューを返す。不可視なら nullopt
            [[nodiscard]] std::optional<SceneView> CollectView(LevelEditorController& editor) noexcept;

            //! 描画先を破棄する。D3DDevice が非所有ポインタを踏まないよう外した後に呼ぶ
            void ReleaseTarget() noexcept { m_surface.Release(); }

            //! 自由視点の見回しドラッグ中か。真の間はキーボードも UI が持つ
            [[nodiscard]] bool IsFreeFlying() const noexcept { return m_freeFlying; }

            //! 見回しドラッグの立ち下がりを返し、前フレーム状態を進める
            //! @return 真ならこのフレームで見回しが終わった。押しっぱなしのキー解除が要る
            [[nodiscard]] bool ConsumeFreeFlyReleased() noexcept;

            //! 全ボタン解放中だけ、カーソルの重なりに合わせて受付を更新する
            void UpdateMouseLatch() noexcept;

            //! ボタン押下中は判定を保ち、パネル外でもドラッグを続ける状態。編集入力の可否に使う
            [[nodiscard]] bool IsMouseLatched() const noexcept { return m_mouseLatch; }

            //! プレイ中の自由視点の画像のラッチ
            //! 左ボタンをゲームの溜めへ渡す状態。パネル内で押したら、離すまで外でも保つ
            [[nodiscard]] bool IsFreeViewLatched() const noexcept { return m_freeViewLatch; }

            [[nodiscard]] bool IsFocused() const noexcept { return m_surface.IsFocused(); }

        private:
            //! プレイ中に自由視点を映すフレームで、ImGui 入力を集めて自由視点カメラへ渡す
            void TickFreeViewInput(LevelEditorController& editor) noexcept;

            ViewportSurface m_surface;
            bool m_editHovered = false; // 編集中にマウスが画像上に居るか。入力を持つのは編集中の Scene
            bool m_mouseLatch = false;
            bool m_freeViewHovered = false; // マウスが自由視点パネルの画像上に居るか
            bool m_freeViewLatch = false;
            bool m_freeFlying = false;
            bool m_wasFreeFlying = false; // 前フレームの見回しドラッグ状態。立ち下がりでキー押下状態を解除する
        };
    } // namespace Editor
} // namespace NS
