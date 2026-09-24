#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Core/Sphere.h"
#include "Runtime/Physics/Capsule.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace NS::Phys
{
    //! @brief 形 1 つと、その世界座標の置き場所
    //! @details 寸法は拡縮を掛け終えた世界の大きさで持つ
    //! 動く body の合成形状へ入れる時は、PhysicsScene::SyncMovingBody が body の座標へ直す
    struct ShapePart
    {
        JPH::ShapeRefC shape;                                            // 形。作れなかった時は null
        NS::Core::Vector3 position{0.0f, 0.0f, 0.0f};                    // 形の原点の世界座標
        NS::Core::Quaternion rotation = NS::Core::Quaternion::Identity; // 形の世界の向き
    };

    //! OBB の中心と 3 軸をそのまま箱の形にする。形を作れなければ形は null
    [[nodiscard]] ShapePart MakeBoxPart(const NS::Core::OBB& box);
    //! 中心と半径をそのまま球の形にする。形を作れなければ形は null
    [[nodiscard]] ShapePart MakeSpherePart(const NS::Core::Sphere& sphere);
    //! capsule.axis の向きのカプセルの形にする。軸が零ベクトルなら Y 軸。形を作れなければ形は null
    [[nodiscard]] ShapePart MakeCapsulePart(const Capsule& capsule);
} // namespace NS::Phys
