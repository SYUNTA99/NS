#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/Render/DrawItem.h"

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

    class Model;

    //! @brief 描く物の管理クラスが、登録の組から描く物へ渡す持ち主の姿
    //! @details root は持ち主の根の世界行列。Collect には固定更新の間を補間した行列、
    //! WorldBounds と SortCenter には今の固定フレームの行列が入る。
    //! model は同じアクターの体をもう一度描く物が元にする Model で、ほかの描く物では nullptr
    struct RenderSource
    {
        Matrix root = Matrix::Identity;
        const Model* model = nullptr;
    };

    //! @brief DrawItem を生成する描画部品のインターフェース。GPU への発行はシーン側が行う
    //! @details 描く物は持ち主のアクターも根も覚えない。管理クラスが登録の組から姿を渡し、出ている間だけ呼ぶ
    class IRenderable : public NonCopyable
    {
    public:
        IRenderable() noexcept = default;
        virtual ~IRenderable() noexcept = default;

        //! 描画は発行せず、自分の DrawItem を out に積むだけにする
        virtual void Collect(const Gfx::RenderContext& context,
                             const RenderSource& source,
                             std::vector<Gfx::DrawItem>& out) = 0;

        //! @brief カリング用のワールド空間 AABB。Scene が視錐台の外を Collect 前に間引く
        //! @details 全描画物が必ず境界を返す。常に描きたいものは全域を覆う AABB を返して自ら申告する
        [[nodiscard]] virtual AABB WorldBounds(const RenderSource& source) const noexcept = 0;

        [[nodiscard]] virtual RenderBucket Bucket() const noexcept { return RenderBucket::Opaque; }

        [[nodiscard]] virtual Vector3 SortCenter(const RenderSource& source) const noexcept
        {
            (void)source;
            return {};
        }

        //! 半透明ソートで距離が同じときの優先度。小さいほど先。既定 0
        [[nodiscard]] virtual int SortPriority() const noexcept { return 0; }
    };

} // namespace NS
