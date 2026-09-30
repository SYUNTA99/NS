#include "Runtime/Object/Scene/HitSensorDirector.h"

#include "Runtime/Object/Actor.h"

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

    bool HitSensorDirector::Checks(HitSensorType attacker, HitSensorType target) noexcept
    {
        // 行が調べる側、列が調べられる側。順は HitSensorType の並び
        // プレイヤーの体 / プレイヤーの体当たり / 物の体 / 範囲
        constexpr int k_Count = static_cast<int>(HitSensorType::Count);
        constexpr bool k_Table[k_Count][k_Count] = {
            {false, false, false, false}, // プレイヤーの体は何も調べない。範囲に調べられる
            {false, false, true, false},  // プレイヤーの体当たりは物の体を調べる
            {false, false, false, false}, // 物の体は何も調べない。体当たりに調べられる
            {true, false, false, false},  // 範囲はプレイヤーの体を調べる
        };
        const int row = static_cast<int>(attacker);
        const int column = static_cast<int>(target);
        if (row < 0 || row >= k_Count || column < 0 || column >= k_Count)
        {
            return false;
        }
        return k_Table[row][column];
    }

    void HitSensorDirector::OnTick()
    {
        // 先に組を全部集めてから呼ぶ。呼んだ先で配置物が増えたり消えたりしても、調べる並びが崩れない
        m_pairs.clear();
        for (HitSensor* attacker : m_sensors)
        {
            if (!attacker->IsValid())
            {
                continue;
            }
            const SensorVolume attackerVolume = attacker->WorldVolume();
            for (HitSensor* target : m_sensors)
            {
                if (target == attacker || target->Owner() == attacker->Owner() || !target->IsValid() ||
                    !Checks(attacker->Type(), target->Type()))
                {
                    continue;
                }
                if (VolumesOverlap(attackerVolume, target->WorldVolume()))
                {
                    m_pairs.emplace_back(attacker, target);
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

    std::vector<HitSensor*> HitSensorDirector::FindOverlaps(const SensorVolume& volume,
                                                            HitSensorType attackerType,
                                                            const Actor* ignore) const
    {
        std::vector<HitSensor*> found;
        for (HitSensor* sensor : m_sensors)
        {
            if (!sensor->IsValid() || sensor->Owner() == ignore || !Checks(attackerType, sensor->Type()))
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
