#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/ColliderComponent.h"
#include "Runtime/Physics/SweptTriangle.h"

#include <vector>

namespace NS::Object
{
    /// @brief 任意三角形群を当たり判定として Scene に登録する Component
    /// @details 球 / 箱で表せない取り込み形状向け。 三角形ソースは glTF の MeshGeometry 等の呼出側が用意する
    /// 三角形は構築時に確定する local data として、描画メッシュとは独立に持つ
    /// StaticMesh は GPU upload 後に CPU 頂点を捨てるため別途渡す
    /// physics へは三角形チャネル経由で渡す。三角形は SweptTriangle と同じく CCW winding 前提
    class MeshColliderComponent : public ColliderComponent
    {
    public:
        /// 空の collider で構築する
        MeshColliderComponent() noexcept;
        /// local 空間の三角形群で構築する。 所有権を移す
        explicit MeshColliderComponent(std::vector<NS::Physics::Triangle> localTriangles) noexcept;

        /// local 三角形群を差し替える。 所有権を移す
        void SetLocalTriangles(std::vector<NS::Physics::Triangle> localTriangles) noexcept;
        /// 現在の local 三角形群
        [[nodiscard]] const std::vector<NS::Physics::Triangle>& LocalTriangles() const noexcept;

        /// owner の world 変換を各頂点に乗せた world 三角形群を返す。 Owner 未登録なら local をそのまま返す
        [[nodiscard]] std::vector<NS::Physics::Triangle> WorldTriangles() const;

        /// 取り込んだ三角形群を physics へ入れる。 空なら何も入れない
        void AddToPhysics(NS::Physics::PhysicsWorld& physics) const override;

        // 三角形群はリフレクションで運べない。 同じ object の collider と揃えて型名だけ登録しておく
        NS_REFLECT_NONE(MeshColliderComponent, ColliderComponent)

    private:
        std::vector<NS::Physics::Triangle> m_localTriangles; // local 空間の三角形群
    };
} // namespace NS::Object
