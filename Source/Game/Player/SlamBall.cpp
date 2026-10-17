#include "Game/Player/SlamBall.h"

#include "NSlib/CoreObject/ClassObjs/Collider.h"

namespace GL
{
    namespace Player
    {
        NS::Sphere SlamBallAt(const NS::Collider& collider, const NS::Vector3& rootPosition) noexcept
        {
            // 立ち姿の下の球が、丸まった後の玉の中心。ChangeCurled が下端を揃えて根を下げるので、この式が成り立つ
            const NS::Phys::Capsule capsule = collider.CapsuleAt(rootPosition);
            return NS::Sphere{capsule.center - capsule.axis * capsule.halfHeight, capsule.radius};
        }
    } // namespace Player
} // namespace GL
