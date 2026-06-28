#pragma once

/// @file MeshColliderComponent.h
/// @brief メッシュ collider Component。 local 三角形群を owner world 変換して WorldTriangles を返す
///
/// @details 任意形状 (glTF 取り込み等) の当たり判定用。 三角形は構築時に確定する local data として持ち、
/// 描画メッシュとは独立に保持する (StaticMesh は GPU upload 後に CPU 頂点を捨てるため別途渡す)
/// physics へは既存の三角形チャネル経由で渡す。 三角形は CCW winding 前提 (SweptTriangle と同方針)

#include "Framework/Math/Math.h"
#include "Framework/Physics/SweptTriangle.h"
#include "Framework/Scene/Component.h"

#include <vector>

namespace NS::Scene
{
    /// 任意三角形群を当たり判定として SceneBase に登録する Component
    /// 球 / 箱で表せない取り込み形状向け。 三角形ソースは呼出側 (glTF の MeshGeometry 等) が用意する
    class MeshColliderComponent : public Component
    {
    public:
        /// 空の collider で構築する
        MeshColliderComponent() noexcept;
        /// local 空間の三角形群で構築する (所有権を移す)
        explicit MeshColliderComponent(std::vector<NS::Physics::Triangle> localTriangles) noexcept;

        /// local 三角形群を差し替える (所有権を移す)
        void SetLocalTriangles(std::vector<NS::Physics::Triangle> localTriangles) noexcept;
        /// 現在の local 三角形群
        [[nodiscard]] const std::vector<NS::Physics::Triangle>& LocalTriangles() const noexcept;

        /// owner の world 変換を各頂点に乗せた world 三角形群を返す。 Owner 未登録なら local をそのまま返す
        [[nodiscard]] std::vector<NS::Physics::Triangle> WorldTriangles() const;

        // 三角形群は反射で運べない。 兄弟 collider と揃えて型名だけ登録しておく
        NS_REFLECT_NONE(MeshColliderComponent)

    private:
        std::vector<NS::Physics::Triangle> m_localTriangles;
    };
} // namespace NS::Scene
