#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/DrawItem.h"
#include "Runtime/Core/Math.h"

#include <cstdint>
#include <vector>

namespace NS::Graphics
{
    struct RenderContext;
}

namespace NS::Object
{
    //! 描画バケット。Opaque は登録順、Transparent はカメラから遠い順にソートされる
    enum class RenderBucket : std::uint8_t
    {
        Opaque,
        Transparent
    };

    //! @brief 描画機能を持つ Component が多重継承する基底インターフェース
    //! @details MeshRendererComponent 等の描画責務を持つ Component は IRenderable を多重継承して
    //! Collect(...) で自分の DrawItem を積む。OnStart で
    //! Owner()->OwningScene()->RegisterRenderable(this) を呼んで自己登録し、OnEndPlay で解除する
    //! 描画発行は Scene が DrawItem を集めて 1 箇所で行うため、Component は GPU を触らない
    class IRenderable : public NS::Core::NonCopyable
    {
    public:
        IRenderable() noexcept = default;
        virtual ~IRenderable() noexcept = default;

        //! Scene::OnRender から呼ばれる。描画は発行せず、自分の DrawItem を out に積むだけにする
        //! Alpha 補間後の transform は context.alpha 経由で取得する
        virtual void Collect(const NS::Graphics::RenderContext& context, std::vector<NS::Graphics::DrawItem>& out) = 0;

        //! @brief カリング用のワールド空間 AABB。Scene が視錐台の外を Collect 前に間引く
        //! @details 全描画物が必ず境界を返す。常に描きたいものは全域を覆う AABB を返して自ら申告する
        [[nodiscard]] virtual NS::Core::AABB WorldBounds() const noexcept = 0;

        //! 自分の描画バケット。既定は Opaque。 透明を持たない既存 Renderable は無改変で従来どおり
        [[nodiscard]] virtual RenderBucket Bucket() const noexcept { return RenderBucket::Opaque; }

        //! 半透明ソート用のワールド空間中心座標。既定は原点で Opaque は未使用
        [[nodiscard]] virtual NS::Core::Vector3 SortCenter() const noexcept { return {}; }

        //! 半透明ソートで距離が同じときの優先度。小さいほど先。既定 0
        [[nodiscard]] virtual int SortPriority() const noexcept { return 0; }
    };

} // namespace NS::Object
