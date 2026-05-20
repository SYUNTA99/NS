#include "ns/physics/swept_aabb.h"

namespace ns::physics
{
    bool SweptCapsuleVsAABB(const Capsule& /*capsule*/,
                            const ns::core::Vector3& /*motion*/,
                            const ns::core::AABB& /*box*/,
                            float& outToi,
                            ns::core::Vector3& outNormal) noexcept
    {
        //  で実装。stub は常に no hit を返す。
        outToi = 1.0f;
        outNormal = ns::core::Vector3{0.0f, 0.0f, 0.0f};
        return false;
    }
} // namespace ns::physics
