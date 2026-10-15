#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/Sphere.h"

namespace NS::Obj
{
    class Collider;
} // namespace NS::Obj

namespace GL::Player
{
    //! @brief 根が rootPosition にある時の、当たりのカプセルの下の球を返す
    //! @details 丸まりで下端を揃えるため、立ち姿と玉で同じ球になる。突進の玉・狙いの線・突進の矢印が同じ球を使う
    [[nodiscard]] NS::Sphere SlamBallAt(const NS::Obj::Collider& collider, const NS::Vector3& rootPosition) noexcept;
} // namespace GL::Player
