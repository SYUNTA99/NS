#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/Collider.h"
#include "Runtime/Physics/Triangle.h"

#include <vector>

namespace NS::Phys
{
    struct MeshCollision;
}

namespace NS::Obj
{
    //! @brief メッシュ資産の三角形を当たりにする Component
    //! @details 球 / 箱で表せない取り込み形状向け
    //! 当たりは AssetManager が資産ごとに持ち、同じ資産を置いた配置物はその形を共有する
    //! 自分が持つのは借りた当たりへの参照だけで、body は自分の位置・回転・拡縮で置く
    class MeshCollider : public Collider
    {
    public:
        //! 空の collider で構築する
        MeshCollider() noexcept;

        //! 使う当たりを差し替える。所有しないので、この Component より長く生きる物を渡す。null で当たり無し
        void SetCollision(const NS::Phys::MeshCollision* collision) noexcept;
        //! 借りている当たり。無ければ null
        [[nodiscard]] const NS::Phys::MeshCollision* Collision() const noexcept;

        //! owner の world 変換を各頂点に乗せた world 三角形群を返す。Owner 未登録なら資産の座標のまま返す
        //! 当たりが無ければ空
        [[nodiscard]] std::vector<NS::Phys::Triangle> WorldTriangles() const;

        //! 同じ object の MeshRenderer の参照から当たりを借りる
        //! 解決できない参照は描画と同じく cube にする
        //! MeshRenderer が無ければ警告を出して当たり無しのまま
        void ResolveAssets(AssetManager& assets) override;

        // 当たりはリフレクションで運ばない。ResolveAssets が描画の参照から借りるので、型名だけ登録しておく
        NS_REFLECT_NONE(MeshCollider, Collider)

    private:
        // 資産の形を自分の位置・回転・拡縮で body 1 個として置く。当たりが無ければ何も入れない
        // 歪みのある変換だけは、WorldTriangles の三角形から自分専用の形を作る
        [[nodiscard]] JPH::BodyID SyncBody(NS::Phys::PhysicsScene& physics, JPH::BodyID current) override;

        const NS::Phys::MeshCollision* m_collision = nullptr; // 非所有。普段は AssetManager の持ち物
    };
} // namespace NS::Obj
