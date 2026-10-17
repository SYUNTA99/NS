#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/DrawItem.h"

#include "NSlib/Core/Types.h"
#include <vector>

namespace NS
{
    namespace Gfx
    {
        struct RenderContext;
    }
} // namespace NS

namespace NS
{
    //! 描画バケット。Opaque は登録順、Transparent はカメラから遠い順にソートされる
    enum class RenderBucket : uint8
    {
        Opaque,
        Transparent
    };

    //! DrawItem を生成する描画部品のインターフェース。GPU への発行はシーン側が行う
    class IRenderable : public NonCopyable
    {
    public:
        IRenderable() noexcept = default;
        virtual ~IRenderable() noexcept = default;

        //! Scene::OnRender から呼ばれる。描画は発行せず、自分の DrawItem を out に積むだけにする
        //! Alpha 補間後の transform は context.alpha 経由で取得する
        virtual void Collect(const Gfx::RenderContext& context, std::vector<Gfx::DrawItem>& out) = 0;

        //! @brief カリング用のワールド空間 AABB。Scene が視錐台の外を Collect 前に間引く
        //! @details 全描画物が必ず境界を返す。常に描きたいものは全域を覆う AABB を返して自ら申告する
        [[nodiscard]] virtual AABB WorldBounds() const noexcept = 0;

        [[nodiscard]] virtual RenderBucket Bucket() const noexcept { return RenderBucket::Opaque; }

        [[nodiscard]] virtual Vector3 SortCenter() const noexcept { return {}; }

        //! 半透明ソートで距離が同じときの優先度。小さいほど先。既定 0
        [[nodiscard]] virtual int SortPriority() const noexcept { return 0; }
    };

} // namespace NS
