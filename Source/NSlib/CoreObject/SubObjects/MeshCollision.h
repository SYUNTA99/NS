#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/SubObjects/Collision.h"
#include "NSlib/Physics/Triangle.h"

#include <vector>

namespace NS
{
    namespace Phys
    {
        struct MeshShape;
    }
} // namespace NS

namespace NS
{
    //! メッシュの三角形による地形。資産ごとに形状を共有し、個体の姿勢で配置する
    class MeshCollision : public Collision
    {
    public:
        explicit MeshCollision(SceneActor* owner) noexcept : Collision(owner), m_owner(owner) {}

        //! 所有しない当たりを差し替える。この SubObject より長く生きる物を渡す。未指定なら当たりなし
        void SetShape(const Phys::MeshShape* shape) noexcept;
        //! 借りている当たり。なければ空
        [[nodiscard]] const Phys::MeshShape* Shape() const noexcept;

        //! 持ち主の世界変換を各頂点に掛け、世界座標の三角形群を返す。持ち主が未登録なら資産の座標のまま
        //! 当たりが無ければ空
        [[nodiscard]] std::vector<Phys::Triangle> WorldTriangles() const;

        // 当たりはリフレクションで運ばない。持ち主の OnAssetsLoaded が Model のメッシュの名前から渡すので、型名だけ登録しておく
        NS_REFLECT_NONE(MeshCollision, Collision)

    private:
        // 資産の形を自分の位置・回転・拡縮で body 1 個として置く。当たりが無ければ何も入れない
        // 歪みのある変換だけは、WorldTriangles の三角形から自分専用の形を作る
        [[nodiscard]] JPH::BodyID SyncBody(Phys::PhysicsScene& physics, JPH::BodyID current) override;

        const Phys::MeshShape* m_shape = nullptr; // 非所有。普段は AssetManager の持ち物
        SceneActor* m_owner = nullptr;            // 持ち主 (非所有)
    };
} // namespace NS
