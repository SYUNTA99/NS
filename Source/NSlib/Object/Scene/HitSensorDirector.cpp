#include "NSlib/Object/Scene/HitSensorDirector.h"

#include "NSlib/Object/Actor.h"

#include <algorithm>

namespace NS::Obj
{
    void HitSensorDirector::Register(HitSensor* sensor)
    {
        if (sensor == nullptr || IsRegistered(sensor))
        {
            return;
        }
        m_sensors.push_back(sensor);
    }

    void HitSensorDirector::Unregister(HitSensor* sensor) noexcept
    {
        std::erase(m_sensors, sensor);
    }

    bool HitSensorDirector::IsRegistered(const HitSensor* sensor) const noexcept
    {
        return std::find(m_sensors.begin(), m_sensors.end(), sensor) != m_sensors.end();
    }

    void HitSensorDirector::OnTick()
    {
        // TODO: 組を総当たりで見ている。センサーが数十を超えたら格子で絞る
        // 先に組を全部集めてから呼ぶ。呼んだ先で配置物が増えたり消えたりしても、調べる並びが崩れない
        m_pairs.clear();
        for (std::size_t i = 0; i < m_sensors.size(); ++i)
        {
            HitSensor* first = m_sensors[i];
            if (!first->IsValid())
            {
                continue;
            }
            const SensorVolume firstVolume = first->WorldVolume();
            for (std::size_t j = i + 1; j < m_sensors.size(); ++j)
            {
                HitSensor* second = m_sensors[j];
                if (second->Owner() == first->Owner() || !second->IsValid())
                {
                    continue;
                }
                if (VolumesOverlap(firstVolume, second->WorldVolume()))
                {
                    m_pairs.emplace_back(first, second);
                    m_pairs.emplace_back(second, first);
                }
            }
        }

        const std::vector<std::pair<HitSensor*, HitSensor*>> pairs = m_pairs;
        for (const std::pair<HitSensor*, HitSensor*>& pair : pairs)
        {
            if (!IsRegistered(pair.first) || !IsRegistered(pair.second))
            {
                continue;
            }
            if (Actor* owner = pair.first->Owner())
            {
                owner->AttackSensor(*pair.first, *pair.second);
            }
        }
    }

    std::vector<HitSensor*> HitSensorDirector::FindOverlaps(const SensorVolume& volume, const Actor* ignore) const
    {
        // TODO: 総当たりで見ている。センサーが数十を超えたら格子で絞る
        std::vector<HitSensor*> found;
        for (HitSensor* sensor : m_sensors)
        {
            if (!sensor->IsValid() || sensor->Owner() == ignore)
            {
                continue;
            }
            if (VolumesOverlap(volume, sensor->WorldVolume()))
            {
                found.push_back(sensor);
            }
        }
        return found;
    }
} // namespace NS::Obj
