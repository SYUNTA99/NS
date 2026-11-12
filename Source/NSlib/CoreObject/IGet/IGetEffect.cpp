#include "NSlib/CoreObject/IGet/IGetEffect.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Types.h"

#include "NSlib/CoreObject/ClassObjs/EffectObj.h"

namespace NS
{
    uint32 PlayEffect(const IGetEffect* user, const Effect* asset, const Gfx::EffectPlayDesc& desc)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        EffectObj* effect = user->GetEffect();
        if (effect == nullptr)
        {
            return 0;
        }
        return effect->Play(asset, desc);
    }

    uint32 PlayFollowingEffect(const IGetEffect* user, const Effect* asset, const Gfx::EffectPlayDesc& desc)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        EffectObj* effect = user->GetEffect();
        if (effect == nullptr)
        {
            return 0;
        }
        return effect->PlayFollowing(asset, desc);
    }

    void StopEffect(const IGetEffect* user, uint32 id) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectObj* effect = user->GetEffect())
        {
            effect->Stop(id);
        }
    }

    void StopEffectRoot(const IGetEffect* user, uint32 id) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectObj* effect = user->GetEffect())
        {
            effect->StopRoot(id);
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
            effect->SetTransform(id, position, rotation, scale);
        }
    }

    void SetEffectDynamicInput(const IGetEffect* user, uint32 id, int index, float value) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectObj* effect = user->GetEffect())
        {
            effect->SetDynamicInput(id, index, value);
        }
    }
} // namespace NS
