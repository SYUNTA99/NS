#include "Game/Level/EffectSwitches.h"

#include "Game/Level/HitTimeline.h"

namespace GL::Level
{
    EffectSwitches& EffectSwitches::Get()
    {
        static EffectSwitches instance;
        return instance;
    }

    void EffectSwitches::AddLayerName(std::string_view name)
    {
        m_layerNames.emplace(name);
    }

    std::vector<std::string> EffectSwitches::TurnOff(const std::vector<std::string>& names)
    {
        std::vector<std::string> rejected;
        for (const std::string& name : names)
        {
            if (!MakeHitEventValue(name).has_value() && !m_layerNames.contains(name))
            {
                rejected.push_back(name);
                continue;
            }
            m_off.insert(name);
        }
        return rejected;
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
} // namespace GL::Level
