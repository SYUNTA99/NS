#include "NSlib/Object/Gravity.h"

#include "NSlib/Object/ActorBase.h"
#include "NSlib/Object/Scene/Scene.h"

#include <cmath>

namespace NS::Obj
{
    NS::Vector3 GravityDirection(const ActorBase& actor) noexcept
    {
        const Scene* scene = actor.OwningScene();
        if (scene == nullptr)
        {
            return NS::Vector3{0.0f, -1.0f, 0.0f};
        }
        return scene->GravityDirection();
    }

    void AddGravity(const ActorBase& actor, NS::Vector3& velocity, float strength, float dt) noexcept
    {
        if (!std::isfinite(strength) || !std::isfinite(dt) || !(dt > 0.0f))
        {
            return;
        }
        velocity += GravityDirection(actor) * (strength * dt);
    }
} // namespace NS::Obj
