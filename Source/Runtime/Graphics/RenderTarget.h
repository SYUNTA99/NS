#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Math/Math.h"

#include <memory>

namespace NS::Graphics
{

    class Texture;

    //! @brief シーンをテクスチャへ描くためのオフスクリーン描画先
    //! @details カラー (RTV+SRV) と深度 (DSV) を 1 組で持ち、Renderer::SetSceneTarget へ渡して使う
    //! 構築に失敗した場合は例外を送出せず、無効な状態として扱う
    class RenderTarget : public NS::Core::NonCopyable
    {
    public:
        //! @brief 指定サイズのオフスクリーン描画先を生成する
        //! @param size 幅と高さ。0 以下の成分があると無効なインスタンスを返す
        //! @return 生成失敗時も非nullのインスタンスを返す（IsValid で判定する）
        [[nodiscard]] static std::unique_ptr<RenderTarget> Create(NS::Math::Size2D size);

        ~RenderTarget();

        [[nodiscard]] bool IsValid() const noexcept;

        //! カラーと深度のサイズを取得する
        [[nodiscard]] NS::Math::Size2D Size() const noexcept;

        //! @brief 描画先のサイズを変更する
        //! @note 同サイズ、または 0 以下の指定は何もしない。旧テクスチャは即座に手放すため、
        //! SRV を参照する描画が終わった位置で呼ぶこと
        void Resize(NS::Math::Size2D size) noexcept;

        //! 描画先となるカラーテクスチャ (RTV+SRV)。無効時は nullptr
        [[nodiscard]] Texture* Color() const noexcept;

        //! 深度テクスチャ (DSV)。無効時は nullptr
        [[nodiscard]] Texture* Depth() const noexcept;

        //! @brief UI (ImGui::Image) へ渡すためのカラー SRV ハンドルを取得する
        //! @return 無効時は nullptr。型を伏せた非所有ポインタで、寿命はこのインスタンスに従う
        [[nodiscard]] void* UiTextureHandle() const noexcept;

    private:
        explicit RenderTarget(NS::Math::Size2D size);

        //! カラーと深度を組で作り直す。片方でも失敗したら両方とも持たない
        void Build(NS::Math::Size2D size) noexcept;

        std::unique_ptr<Texture> m_color; // カラー (RTV+SRV)
        std::unique_ptr<Texture> m_depth; // 深度 (DSV)
    };

} // namespace NS::Graphics
