#pragma once

#include "Editor/ViewportSurface.h"
#include "NSlib/Core/NonCopyable.h"

class LevelEditorController;

namespace NS
{
    namespace Editor
    {
        class TimelinePreview;
        //! ゲーム視点を映すパネル。ゲーム操作の入力はプレイ中だけ受け付ける
        class GameViewPanel : public NonCopyable
        {
        public:
            //! 出荷時の画面比。パネルをどう並べてもゲームの見え方が変わらないようここで固定する
            static constexpr float k_GameAspect = 16.0f / 9.0f;

            GameViewPanel() noexcept { m_surface.SetFixedAspect(k_GameAspect); }

            //! 描画先の初期目標サイズを窓サイズに合わせる
            void SetInitialSize(const Size2D& size) noexcept { m_surface.SetInitialSize(size); }

            //! フレーム先頭で可視状態を false にする。描画パス (通常 / 全面化) が立て直す
            void ResetVisibility() noexcept { m_surface.ResetVisibility(); }

            //! Game パネルを描き、プレイ中は入力矩形とカーソルの重なりを controller へ渡す
            void Render(LevelEditorController& editor) noexcept;

            //! 中央以外を全面化する間、前面の矩形とカーソルの重なりを無効化する
            void Suppress(LevelEditorController& editor) noexcept;

            //! 描画先とゲーム視点のビューを返す
            //! @return 可視ならビュー、不可視なら nullopt
            [[nodiscard]] std::optional<SceneView> CollectView(LevelEditorController& editor) noexcept;

            //! 貼り付け中の描画先へ、下見の現在のフレームを描く
            //! @return 描画できた場合 true、それ以外の場合は false
            bool RenderPreview(TimelinePreview& preview);

            //! 描画先を破棄する。D3DDevice が非所有ポインタを踏まないよう外した後に呼ぶ
            void ReleaseTarget() noexcept { m_surface.Release(); }

            //! 全ボタン解放中だけ、カーソルの重なりに合わせて受付を更新する
            void UpdateMouseLatch() noexcept;

            //! ボタン押下中は判定を保ち、パネル外でもドラッグを続ける状態
            [[nodiscard]] bool IsMouseLatched() const noexcept { return m_mouseLatch; }

            [[nodiscard]] bool IsFocused() const noexcept { return m_surface.IsFocused(); }

        private:
            ViewportSurface m_surface;
            bool m_hovered = false;    // マウスが Game パネルの画像上に居るか
            bool m_mouseLatch = false; // ボタン押下中は判定を保ち、パネル外でもドラッグを続ける
        };
    } // namespace Editor
} // namespace NS
