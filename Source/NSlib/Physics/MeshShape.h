#pragma once

#include "NSlib/Physics/Triangle.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <span>
#include <vector>

namespace NS::Phys
{
    //! 静的 body 用の三角形と Jolt 形状。歪みのない配置物間で形状を共有する
    struct MeshShape
    {
        std::vector<Triangle> triangles; // 資産の座標。法線 (v1 - v0) × (v2 - v0) が外を向く並び
        JPH::ShapeRefC shape;            // triangles から作る形。未生成・生成失敗時は空
    };

    //! Jolt を初期化して三角形を作る。入力なし・生成失敗時は空
    [[nodiscard]] JPH::ShapeRefC CreateMeshShape(std::span<const Triangle> triangles);
} // namespace NS::Phys
