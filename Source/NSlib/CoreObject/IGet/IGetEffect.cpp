#include "NSlib/CoreObject/IGet/IGetEffect.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Types.h"

#include "NSlib/CoreObject/ClassObjs/EffectObj.h"

namespace NS
{
    void PreloadEffects(const IGetEffect* user, std::initializer_list<const Effect*> assets)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        EffectObj* effect = user->GetEffect();
        if (effect == nullptr)
        {
            return;
        }
        for (const Effect* asset : assets)
        {
            effect->Preload(user->GetEffectScene(), asset);
        }
    }

    uint32 PlayEffect(const IGetEffect* user, const Effect* asset, const Gfx::EffectPlayDesc& desc)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        EffectObj* effect = user->GetEffect();
        if (effect == nullptr)
        {
            return 0;
        }
        return effect->Play(user->GetEffectScene(), asset, desc);
    }

    void StopEffect(const IGetEffect* user, uint32 id) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectObj* effect = user->GetEffect())
        {
            effect->Stop(user->GetEffectScene(), id);
        }
    }

    void StopEffectRoot(const IGetEffect* user, uint32 id) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectObj* effect = user->GetEffect())
        {
            effect->StopRoot(user->GetEffectScene(), id);
        }
    }

    void StopEffectAfter(const IGetEffect* user, uint32 id, int steps)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectObj* effect = user->GetEffect())
        {
            effect->StopAfter(id, steps);
        }
    }

    void SetEffectTransform(const IGetEffect* user,
                            uint32 id,
                            const Vector3& position,
                            const Quaternion& rotation,
                            const Vector3& scale) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectObj* effect = user->GetEffect())
        {
            effect->SetTransform(user->GetEffectScene(), id, position, rotation, scale);
        }
    }

    void SetEffectDynamicInput(const IGetEffect* user, uint32 id, int index, float value) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectObj* effect = user->GetEffect())
        {
            effect->SetDynamicInput(user->GetEffectScene(), id, index, value);
        }
    }

    void SetEffectAmount(const IGetEffect* user, uint32 id, float amount) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectObj* effect = user->GetEffect())
        {
            effect->SetAmount(id, amount);
        }
    }

    void SetEffectRotation(const IGetEffect* user, uint32 id, const Quaternion& rotation) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectObj* effect = user->GetEffect())
        {
            effect->SetRotation(id, rotation);
        }
    }

    const EffectRecord* FindEffect(const IGetEffect* user, uint32 id) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const EffectObj* effect = user->GetEffect();
        if (effect == nullptr)
        {
            return nullptr;
        }
        return effect->Find(id);
    }

    int GetEffectStep(const IGetEffect* user) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const EffectObj* effect = user->GetEffect();
        if (effect == nullptr)
        {
            return 0;
        }
        return effect->Step();
    }
} // namespace NS
