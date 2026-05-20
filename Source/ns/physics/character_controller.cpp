#include "ns/physics/character_controller.h"

namespace ns::physics
{
    CharacterControllerResult CharacterController::Update(const CharacterControllerInput& input) noexcept
    {
        //  で実装。stub: 衝突解決なしで velocity*dt を加算するだけ。
        CharacterControllerResult result;
        result.position = input.position + input.velocity * input.dt;
        result.velocity = input.velocity;
        result.grounded = false;
        result.contactNormal = ns::core::Vector3{0.0f, 0.0f, 0.0f};
        return result;
    }
} // namespace ns::physics
