#include "NSlib/CoreObject/IUse/IUseEffect.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Types.h"

#include "NSlib/CoreObject/ClassObjs/Effect.h"

namespace NS
{
    void PreloadEffects(const IUseEffect* user, std::initializer_list<EffectName> names)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        Effect* effect = user->GetEffect();
        if (effect == nullptr)
        {
            return;
        }
        for (const EffectName& name : names)
        {
            effect->Preload(user->GetEffectScene(), name.name);
        }
    }

    uint32 PlayEffect(const IUseEffect* user, const EffectName& name, const Gfx::EffectPlayDesc& desc)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        Effect* effect = user->GetEffect();
        if (effect == nullptr)
        {
            return 0;
        }
        return effect->Play(user->GetEffectScene(), name.name, desc);
    }

    void StopEffect(const IUseEffect* user, uint32 id) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (Effect* effect = user->GetEffect())
        {
            effect->Stop(user->GetEffectScene(), id);
        }
    }

    void StopEffectRoot(const IUseEffect* user, uint32 id) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (Effect* effect = user->GetEffect())
        {
            effect->StopRoot(user->GetEffectScene(), id);
        }
    }

    void StopEffectAfter(const IUseEffect* user, uint32 id, int steps)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (Effect* effect = user->GetEffect())
        {
            effect->StopAfter(id, steps);
        }
    }

    void SetEffectTransform(const IUseEffect* user,
                            uint32 id,
                            const Vector3& position,
                            const Quaternion& rotation,
                            const Vector3& scale) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (Effect* effect = user->GetEffect())
        {
            effect->SetTransform(user->GetEffectScene(), id, position, rotation, scale);
        }
    }

    void SetEffectDynamicInput(const IUseEffect* user, uint32 id, int index, float value) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (Effect* effect = user->GetEffect())
        {
            effect->SetDynamicInput(user->GetEffectScene(), id, index, value);
        }
    }

    void SetEffectAmount(const IUseEffect* user, uint32 id, float amount) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (Effect* effect = user->GetEffect())
        {
            effect->SetAmount(id, amount);
        }
    }

    void SetEffectRotation(const IUseEffect* user, uint32 id, const Quaternion& rotation) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (Effect* effect = user->GetEffect())
        {
            effect->SetRotation(id, rotation);
        }
    }

    const EffectRecord* FindEffect(const IUseEffect* user, uint32 id) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const Effect* effect = user->GetEffect();
        if (effect == nullptr)
        {
            return nullptr;
        }
        return effect->Find(id);
    }

    int GetEffectStep(const IUseEffect* user) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const Effect* effect = user->GetEffect();
        if (effect == nullptr)
        {
            return 0;
        }
        return effect->Step();
    }
} // namespace NS
