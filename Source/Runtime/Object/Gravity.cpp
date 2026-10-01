#include "Runtime/Object/Gravity.h"

#include "Runtime/Object/ActorBase.h"
#include "Runtime/Object/Scene/Scene.h"

#include <cmath>

namespace NS::Obj
{
    NS::Core::Vector3 GravityDirection(const ActorBase& actor) noexcept
    {
        const Scene* scene = actor.OwningScene();
        if (scene == nullptr)
        {
            return NS::Core::Vector3{0.0f, -1.0f, 0.0f};
        }
        return scene->GravityDirection();
    }

    void AddGravity(const ActorBase& actor, NS::Core::Vector3& velocity, float strength, float dt) noexcept
    {
        if (!std::isfinite(strength) || !std::isfinite(dt) || !(dt > 0.0f))
        {
            return;
        }
        velocity += GravityDirection(actor) * (strength * dt);
    }
} // namespace NS::Obj
