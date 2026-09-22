#pragma once

#include "Runtime/Physics/Triangle.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <span>
#include <vector>

namespace NS::Phys
{
    //! @brief メッシュ 1 つ分の当たり。資産の座標の三角形と、そこから作った形
    //! @details 形は参照数つきで、置いた body がそれぞれ参照を持つ
    //! 同じ資産を置いた配置物は、歪みが無ければ形を作り直さずに共有する
    //! Jolt の三角形の形は動かない body 専用
    struct MeshCollision
    {
        std::vector<Triangle> triangles; // 資産の座標。法線 (v1 - v0) × (v2 - v0) が外を向く並び
        JPH::ShapeRefC shape;            // triangles から作った形。まだ作っていないか、作れなければ null
    };

    //! @brief 三角形群から Jolt の三角形の形を作る
    //! @details PhysicsScene を 1 つも作る前に呼んでも、Jolt の下準備を先に通してから作る
    //! @param[in] triangles 形にする三角形群
    //! @return 作った形。空か、Jolt が形を作れなければ null
    [[nodiscard]] JPH::ShapeRefC CreateMeshShape(std::span<const Triangle> triangles);
} // namespace NS::Phys
