#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/Sphere.h"

namespace NS
{
    class Collider;
} // namespace NS

namespace GL
{
    namespace Player
    {
        //! 根が rootPosition にある時の、当たりのカプセルの下の球を返す
        //! 丸まりで下端を揃えるため、立ち姿と玉で同じ球になる。突進の玉・狙いの線・突進の矢印が同じ球を使う
        [[nodiscard]] NS::Sphere SlamBallAt(const NS::Collider* collider, const NS::Vector3& rootPosition) noexcept;
    } // namespace Player
} // namespace GL
