#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"

#include <memory>
#include <optional>

struct ImVec2;

namespace NS::Gfx
{
        class D3DRenderTarget;
} // namespace NS::Gfx

namespace NS
{
    struct SceneView;
    struct CameraPose;
} // namespace NS

namespace NS::Editor
{
        //! パネルに貼るオフスクリーン描画先。画像表示後に次フレームのサイズへ合わせる
        class ViewportSurface : public NonCopyable
        {
        public:
            // D3DRenderTarget の完全型が必要なため、構築と破棄は .cpp で定義する
            ViewportSurface();
            ~ViewportSurface();

            //! 描画先の初期目標サイズを窓サイズに合わせ、初回フレームから映せるようにする
            void SetInitialSize(const Size2D& size) noexcept { m_size = size; }

            //! 映像の縦横比を固定する。0 以下ならパネルの形に追従する
            void SetFixedAspect(float aspect) noexcept { m_fixedAspect = aspect; }

            //! フレーム先頭で可視状態と焦点を false にする。このフレームに BeginView されなければ不可視のままになる
            void ResetVisibility() noexcept
            {
                m_visible = false;
                m_focused = false;
                m_tabClicked = false;
            }

            //! パネルに画像を貼れた時だけ矩形とカーソルの重なりを更新し、true を返す
            //! 戻り値に関わらず EndView を呼ぶ
            [[nodiscard]] bool BeginView(const char* windowName,
                                         ImVec2& outMin,
                                         ImVec2& outMax,
                                         bool& outHovered) noexcept;

            //! BeginView と対で必ず呼ぶ。Begin した窓を閉じ、積んだスタイルを戻す
            void EndView() noexcept;

            //! @brief 目標サイズに追従して描画先を用意し、pose とセットのビューを返す
            //! @return 可視だったフレームは {描画先, pose}。不可視やサイズ不足なら nullopt
            [[nodiscard]] std::optional<SceneView> CollectView(std::optional<CameraPose> pose) noexcept;

            //! @brief 貼り付け中の描画先をリサイズせず、pose とセットで返す
            //! @details 画像の実描画前に使い、CollectView の前に使い終える
            //! @return 可視かつ描画先が有効ならビュー、それ以外なら nullopt
            [[nodiscard]] std::optional<SceneView> CurrentView(std::optional<CameraPose> pose) const noexcept;

            //! 描画先を破棄する。D3DDevice が非所有ポインタを踏まないよう外した後に呼ぶ
            void Release() noexcept;

            //! このフレームにパネルが可視 (サイズを持つ) だったか
            [[nodiscard]] bool IsVisible() const noexcept { return m_visible; }

            //! このフレームにパネルへ焦点があったか。プレイ中のキーボードをゲームへ渡す条件に使う
            [[nodiscard]] bool IsFocused() const noexcept { return m_focused; }

            //! タブの移動を妨げないよう、ドラッグせず見出し上で左ボタンを離したフレームだけ true。
            [[nodiscard]] bool WasTabClicked() const noexcept { return m_tabClicked; }

        private:
            std::unique_ptr<Gfx::D3DRenderTarget> m_target;
            Size2D m_size{0, 0}; // content 領域。次フレームの描画先サイズ
            float m_fixedAspect = 0.0f;
            bool m_visible = false;
            bool m_focused = false;
            bool m_tabPressed = false;
            bool m_tabClicked = false;
        };
} // namespace NS::Editor
