#include "NSlib/Object/IUse/IUseEffect.h"

#include "NSlib/Object/SubObjects/EffectKeeper.h"

namespace NS::Obj
{
    void PreloadEffects(const IUseEffect& user, std::initializer_list<std::string_view> names)
    {
        EffectKeeper* keeper = user.GetEffectKeeper();
        if (keeper == nullptr)
        {
            return;
        }
        for (const std::string_view name : names)
        {
            keeper->Preload(name);
        }
    }

    std::uint32_t PlayEffect(const IUseEffect& user, std::string_view name, const NS::Gfx::EffectPlayDesc& desc)
    {
        EffectKeeper* keeper = user.GetEffectKeeper();
        if (keeper == nullptr)
        {
            return 0;
        }
        return keeper->Play(name, desc);
    }

    void StopEffect(const IUseEffect& user, std::uint32_t id) noexcept
    {
        if (EffectKeeper* keeper = user.GetEffectKeeper())
        {
            keeper->Stop(id);
        }
    }

    void StopEffectRoot(const IUseEffect& user, std::uint32_t id) noexcept
    {
        if (EffectKeeper* keeper = user.GetEffectKeeper())
        {
            keeper->StopRoot(id);
        }
    }

    void StopEffectAfter(const IUseEffect& user, std::uint32_t id, int steps)
    {
        if (EffectKeeper* keeper = user.GetEffectKeeper())
        {
            keeper->StopAfter(id, steps);
        }
    }

    void SetEffectTransform(const IUseEffect& user,
                            std::uint32_t id,
                            const NS::Vector3& position,
                            const NS::Quaternion& rotation,
                            const NS::Vector3& scale) noexcept
    {
        if (EffectKeeper* keeper = user.GetEffectKeeper())
        {
            keeper->SetTransform(id, position, rotation, scale);
        }
    }

    void SetEffectDynamicInput(const IUseEffect& user, std::uint32_t id, int index, float value) noexcept
    {
        if (EffectKeeper* keeper = user.GetEffectKeeper())
        {
            keeper->SetDynamicInput(id, index, value);
        }
    }

    void SetEffectAmount(const IUseEffect& user, std::uint32_t id, float amount) noexcept
    {
        if (EffectKeeper* keeper = user.GetEffectKeeper())
        {
            keeper->SetAmount(id, amount);
        }
    }

    void SetEffectRotation(const IUseEffect& user, std::uint32_t id, const NS::Quaternion& rotation) noexcept
    {
        if (EffectKeeper* keeper = user.GetEffectKeeper())
        {
            keeper->SetRotation(id, rotation);
        }
    }

    const EffectRecord* FindEffect(const IUseEffect& user, std::uint32_t id) noexcept
    {
        const EffectKeeper* keeper = user.GetEffectKeeper();
        if (keeper == nullptr)
        {
            return nullptr;
        }
        return keeper->Find(id);
    }

    int EffectStep(const IUseEffect& user) noexcept
    {
        const EffectKeeper* keeper = user.GetEffectKeeper();
        if (keeper == nullptr)
        {
            return 0;
        }
        return keeper->Step();
    }
} // namespace NS::Obj
