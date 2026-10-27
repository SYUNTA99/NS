#include "NSlib/CoreObject/IGet/IGetEffect.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Types.h"

#include "NSlib/CoreObject/ClassObjs/EffectKeeper.h"

namespace NS
{
    void PreloadEffects(const IGetEffect* user, std::initializer_list<EffectName> names)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        EffectKeeper* keeper = user->GetEffectKeeper();
        if (keeper == nullptr)
        {
            return;
        }
        for (const EffectName& name : names)
        {
            keeper->Preload(user->GetEffectScene(), name.name);
        }
    }

    uint32 PlayEffect(const IGetEffect* user, const EffectName& name, const Gfx::EffectPlayDesc& desc)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        EffectKeeper* keeper = user->GetEffectKeeper();
        if (keeper == nullptr)
        {
            return 0;
        }
        return keeper->Play(user->GetEffectScene(), name.name, desc);
    }

    void StopEffect(const IGetEffect* user, uint32 id) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectKeeper* keeper = user->GetEffectKeeper())
        {
            keeper->Stop(user->GetEffectScene(), id);
        }
    }

    void StopEffectRoot(const IGetEffect* user, uint32 id) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectKeeper* keeper = user->GetEffectKeeper())
        {
            keeper->StopRoot(user->GetEffectScene(), id);
        }
    }

    void StopEffectAfter(const IGetEffect* user, uint32 id, int steps)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectKeeper* keeper = user->GetEffectKeeper())
        {
            keeper->StopAfter(id, steps);
        }
    }

    void SetEffectTransform(const IGetEffect* user,
                            uint32 id,
                            const Vector3& position,
                            const Quaternion& rotation,
                            const Vector3& scale) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectKeeper* keeper = user->GetEffectKeeper())
        {
            keeper->SetTransform(user->GetEffectScene(), id, position, rotation, scale);
        }
    }

    void SetEffectDynamicInput(const IGetEffect* user, uint32 id, int index, float value) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectKeeper* keeper = user->GetEffectKeeper())
        {
            keeper->SetDynamicInput(user->GetEffectScene(), id, index, value);
        }
    }

    void SetEffectAmount(const IGetEffect* user, uint32 id, float amount) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectKeeper* keeper = user->GetEffectKeeper())
        {
            keeper->SetAmount(id, amount);
        }
    }

    void SetEffectRotation(const IGetEffect* user, uint32 id, const Quaternion& rotation) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectKeeper* keeper = user->GetEffectKeeper())
        {
            keeper->SetRotation(id, rotation);
        }
    }

    const EffectRecord* FindEffect(const IGetEffect* user, uint32 id) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const EffectKeeper* keeper = user->GetEffectKeeper();
        if (keeper == nullptr)
        {
            return nullptr;
        }
        return keeper->Find(id);
    }

    int GetEffectStep(const IGetEffect* user) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const EffectKeeper* keeper = user->GetEffectKeeper();
        if (keeper == nullptr)
        {
            return 0;
        }
        return keeper->Step();
    }
} // namespace NS
