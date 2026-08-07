#pragma once

#include "Editor/ViewportSurface.h"
#include "Runtime/Core/NonCopyable.h"

struct ImVec2;

class LevelEditorController;

namespace NS::Editor
{
    //! @brief 編集ビューを映すパネル。 編集中は入力とギズモ、 プレイ中は自由視点で world を見回す
    //! @details 自前の視点操作 (見回し / パン / ズーム) と向き表示、 Assets からのメッシュ / マテリアルの
    //! 持ち込み口を持つ。 描画先は ViewportSurface が握る
    class SceneViewPanel : public NS::Core::NonCopyable
    {
    public:
        //! 描画先の初期目標サイズを窓サイズに合わせる
        void SetInitialSize(const NS::Core::Size2D& size) noexcept { m_surface.SetInitialSize(size); }

        //! フレーム先頭で可視状態を false にする。 描画パス (通常 / 全面化) が立て直す
        void ResetVisibility() noexcept { m_surface.ResetVisibility(); }

        //! Scene パネルを 1 枚描く。 編集中は入力矩形、 プレイ中は自由視点入力を controller へ渡す
        void Render(LevelEditorController& editor) noexcept;

        //! 中央以外を全面化する間、 前面の矩形と hover を無効化する
        void Suppress(LevelEditorController& editor) noexcept;

        //! F5 全画面直描き中は中央矩形と自由視点の上書きを外す
        void ClearForHiddenUi(LevelEditorController& editor) noexcept;

        //! @return 可視なら {RT, 編集 / 自由視点} のビュー。 不可視なら nullopt
        [[nodiscard]] std::optional<NS::Object::SceneView> CollectView(LevelEditorController& editor) noexcept;

        //! 描画先を破棄する。 Renderer が非所有ポインタを踏まないよう外した後に呼ぶ
        void ReleaseTarget() noexcept { m_surface.Release(); }

        //! 自由視点の見回しドラッグ中か。 真の間はキーボードも UI が持つ
        [[nodiscard]] bool IsFreeFlying() const noexcept { return m_freeFlying; }

        //! @brief 見回しドラッグの立ち下がりを返し、 前フレーム状態を進める
        //! @return 真ならこのフレームで見回しが終わった。 押しっぱなしのキー解除が要る
        [[nodiscard]] bool ConsumeFreeFlyReleased() noexcept;

        //! 全マウス解放中だけ hover に追従してラッチを更新する
        void UpdateMouseLatch() noexcept;

        //! ボタン押下中は hover を凍結し、 パネル発のドラッグを外まで続けさせる状態。 編集中の入力ゲートに使う
        [[nodiscard]] bool IsMouseLatched() const noexcept { return m_mouseLatch; }

    private:
        //! プレイ中に自由視点を映すフレームで、 ImGui 入力を集めて free-fly カメラへ渡す
        void TickFreeViewInput(LevelEditorController& editor) noexcept;

        ViewportSurface m_surface;
        bool m_editHovered = false;     // 編集中にマウスが画像上に居るか。 入力を持つのは編集中の Scene
        bool m_mouseLatch = false;      // ボタン押下中は hover を凍結し、 パネル発のドラッグを外まで続けさせる
        bool m_freeViewHovered = false; // マウスが自由視点パネルの画像上に居るか
        bool m_freeViewLatch = false;   // ボタン押下中は hover を凍結し、 画像発のドラッグを外まで続けさせる
        bool m_freeFlying = false;      // 自由視点の見回しドラッグ中か。 真の間はキーボードも UI が持つ
        bool m_wasFreeFlying = false;   // 前フレームの見回しドラッグ状態。 立ち下がりでキー押下状態を解除する
    };
} // namespace NS::Editor
