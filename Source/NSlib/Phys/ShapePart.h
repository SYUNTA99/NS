#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/Phys/Capsule.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace NS
{
    namespace Phys
    {
        //! 拡縮済みの世界座標の形状。合成 body の登録時に body 内の座標へ変換する
        struct ShapePart
        {
            JPH::ShapeRefC shape;                               // 形。生成失敗時は空
            Vector3 position{0.0f, 0.0f, 0.0f};             // 形の原点の世界座標
            Quaternion rotation = Quaternion::Identity; // 形の世界の向き
        };

        //! OBB の中心と 3 軸から箱を作る。生成失敗時は空
        [[nodiscard]] ShapePart MakeBoxPart(const OBB& box);
        //! 中心と半径から球を作る。生成失敗時は空
        [[nodiscard]] ShapePart MakeSpherePart(const Sphere& sphere);
        //! capsule.axis の向きでカプセルを作る。軸が零なら Y 軸。生成失敗時は空
        [[nodiscard]] ShapePart MakeCapsulePart(const Capsule& capsule);
    } // namespace Phys
} // namespace NS
