#include "NSlib/Object/IUse/IUseEffect.h"
#include "NSlib/Core/Assert.h"

#include "NSlib/Object/SubObjects/EffectKeeper.h"

namespace NS::Obj
{
    void PreloadEffects(const IUseEffect* user, std::initializer_list<std::string_view> names)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        EffectKeeper* keeper = user->GetEffectKeeper();
        if (keeper == nullptr)
        {
            return;
        }
        for (const std::string_view name : names)
        {
            keeper->Preload(name);
        }
    }

    std::uint32_t PlayEffect(const IUseEffect* user, std::string_view name, const NS::Gfx::EffectPlayDesc& desc)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        EffectKeeper* keeper = user->GetEffectKeeper();
        if (keeper == nullptr)
        {
            return 0;
        }
        return keeper->Play(name, desc);
    }

    void StopEffect(const IUseEffect* user, std::uint32_t id) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectKeeper* keeper = user->GetEffectKeeper())
        {
            keeper->Stop(id);
        }
    }

    void StopEffectRoot(const IUseEffect* user, std::uint32_t id) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectKeeper* keeper = user->GetEffectKeeper())
        {
            keeper->StopRoot(id);
        }
    }

    void StopEffectAfter(const IUseEffect* user, std::uint32_t id, int steps)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectKeeper* keeper = user->GetEffectKeeper())
        {
            keeper->StopAfter(id, steps);
        }
    }

    void SetEffectTransform(const IUseEffect* user,
                            std::uint32_t id,
                            const NS::Vector3& position,
                            const NS::Quaternion& rotation,
                            const NS::Vector3& scale) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectKeeper* keeper = user->GetEffectKeeper())
        {
            keeper->SetTransform(id, position, rotation, scale);
        }
    }

    void SetEffectDynamicInput(const IUseEffect* user, std::uint32_t id, int index, float value) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectKeeper* keeper = user->GetEffectKeeper())
        {
            keeper->SetDynamicInput(id, index, value);
        }
    }

    void SetEffectAmount(const IUseEffect* user, std::uint32_t id, float amount) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectKeeper* keeper = user->GetEffectKeeper())
        {
            keeper->SetAmount(id, amount);
        }
    }

    void SetEffectRotation(const IUseEffect* user, std::uint32_t id, const NS::Quaternion& rotation) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (EffectKeeper* keeper = user->GetEffectKeeper())
        {
            keeper->SetRotation(id, rotation);
        }
    }

    const EffectRecord* FindEffect(const IUseEffect* user, std::uint32_t id) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const EffectKeeper* keeper = user->GetEffectKeeper();
        if (keeper == nullptr)
        {
            return nullptr;
        }
        return keeper->Find(id);
    }

    int EffectStep(const IUseEffect* user) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const EffectKeeper* keeper = user->GetEffectKeeper();
        if (keeper == nullptr)
        {
            return 0;
        }
        return keeper->Step();
    }
} // namespace NS::Obj
