#pragma once

#include "Editor/ViewportSurface.h"
#include "Runtime/Core/NonCopyable.h"

class LevelEditorController;

namespace NS::Editor
{
    //! @brief ゲーム視点の出力を映すパネル。 プレイ中は入力を持ち、 編集中はゲームカメラを貼るだけ
    //! @details 出力を映すのが役目で、 自由視点のようなナビゲーションは持たない。 描画先は ViewportSurface が握る
    class GameViewPanel : public NS::Core::NonCopyable
    {
    public:
        //! 出荷時の画面比。 パネルをどう並べてもゲームの見え方が変わらないようここで固定する
        static constexpr float k_GameAspect = 16.0f / 9.0f;

        GameViewPanel() noexcept { m_surface.SetFixedAspect(k_GameAspect); }

        //! 描画先の初期目標サイズを窓サイズに合わせる
        void SetInitialSize(const NS::Core::Size2D& size) noexcept { m_surface.SetInitialSize(size); }

        //! フレーム先頭で可視状態を false にする。 描画パス (通常 / 全面化) が立て直す
        void ResetVisibility() noexcept { m_surface.ResetVisibility(); }

        //! Game パネルを 1 枚描く。 プレイ中は入力矩形と hover を controller へ渡す
        void Render(LevelEditorController& editor) noexcept;

        //! 中央以外を全面化する間、 前面の矩形と hover を無効化する
        void Suppress(LevelEditorController& editor) noexcept;

        //! @return 可視なら {RT, ゲーム視点} のビュー。 不可視なら nullopt
        [[nodiscard]] std::optional<NS::Object::SceneView> CollectView(LevelEditorController& editor) noexcept;

        //! 描画先を破棄する。 Renderer が非所有ポインタを踏まないよう外した後に呼ぶ
        void ReleaseTarget() noexcept { m_surface.Release(); }

        //! 全マウス解放中だけ hover に追従してラッチを更新する
        void UpdateMouseLatch() noexcept;

        //! ボタン押下中は hover を凍結し、 パネル発のドラッグを外まで続けさせる状態
        [[nodiscard]] bool IsMouseLatched() const noexcept { return m_mouseLatch; }

    private:
        ViewportSurface m_surface;
        bool m_hovered = false;    // マウスが Game パネルの画像上に居るか
        bool m_mouseLatch = false; // ボタン押下中は hover を凍結し、 ドラッグを外まで続けさせる
    };
} // namespace NS::Editor
