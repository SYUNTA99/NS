#include "NSlib/Object/EffectSwitches.h"

namespace NS::Obj
{
    EffectSwitches& EffectSwitches::Get()
    {
        static EffectSwitches instance;
        return instance;
    }

    void EffectSwitches::AddEffectName(std::string_view name)
    {
        m_effectNames.emplace(name);
    }

    void EffectSwitches::TurnOff(std::string_view name)
    {
        m_off.emplace(name);
    }

    void EffectSwitches::TurnOn(std::string_view name)
    {
        const std::set<std::string, std::less<>>::const_iterator found = m_off.find(name);
        if (found != m_off.end())
        {
            m_off.erase(found);
        }
    }

    void EffectSwitches::TurnOnAll() noexcept
    {
        m_off.clear();
    }

    bool EffectSwitches::IsOff(std::string_view name) const
    {
        return m_off.contains(name);
    }
} // namespace NS::Obj
