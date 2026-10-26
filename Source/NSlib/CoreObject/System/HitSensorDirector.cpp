#include "NSlib/CoreObject/System/HitSensorDirector.h"

#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>

namespace NS
{
    namespace
    {
        [[nodiscard]] bool ContainsPair(const std::vector<std::pair<HitSensor*, HitSensor*>>& pairs,
                                        const std::pair<HitSensor*, HitSensor*>& pair) noexcept
        {
            return std::find(pairs.begin(), pairs.end(), pair) != pairs.end();
        }
    } // namespace

    void HitSensorDirector::Register(HitSensor* sensor)
    {
        if (sensor == nullptr || IsRegistered(sensor))
        {
            return;
        }
        m_sensors.push_back(sensor);
    }

    void HitSensorDirector::Unregister(HitSensor* sensor)
    {
        std::erase(m_sensors, sensor);
        // 外したセンサーは解放されて番地が別のセンサーに使われ得るので、覚えた組から番地を全部消す。
        // 自分の向きの組はこの場で終わりを届け、相手の向きの組は相手へ終わりを届ける予約に移す
        std::vector<SensorPair> ownExits;
        for (const SensorPair& pair : m_pendingExits)
        {
            if (pair.first == sensor)
            {
                ownExits.push_back(pair);
            }
        }
        std::erase_if(m_pendingExits, [sensor](const SensorPair& pair) { return pair.first == sensor; });
        for (const SensorPair& pair : m_overlaps)
        {
            if (pair.first == sensor)
            {
                ownExits.push_back(pair);
            }
            else if (pair.second == sensor)
            {
                m_pendingExits.emplace_back(pair.first, nullptr);
            }
        }
        std::erase_if(m_overlaps,
                      [sensor](const SensorPair& pair) { return pair.first == sensor || pair.second == sensor; });

        // 覚えた組を直し終えてから呼ぶので、受け手がセンサーを付け外ししても組は崩れない
        SceneActor* owner = sensor->Host();
        if (owner == nullptr)
        {
            return;
        }
        for (const SensorPair& pair : ownExits)
        {
            HitSensor* other = pair.second;
            if (other != nullptr && !IsRegistered(other))
            {
                other = nullptr;
            }
            owner->OnSensorExit(sensor, other);
        }
    }

    bool HitSensorDirector::IsRegistered(const HitSensor* sensor) const noexcept
    {
        return std::find(m_sensors.begin(), m_sensors.end(), sensor) != m_sensors.end();
    }

    bool HitSensorDirector::IsPresent(const HitSensor* sensor) const noexcept
    {
        NS_ASSERT(Scene, sensor != nullptr, "IsPresent: センサーが null");
        return IsRegistered(sensor) && sensor->IsValidBySystem();
    }

    void HitSensorDirector::OnTick()
    {
        // 先に組を全部集めてから知らせる。知らせた先でアクターが増えたり消えたりしても、調べる並びが崩れない
        NotifyOverlaps(CollectOverlaps());
    }

    std::vector<HitSensorDirector::SensorPair> HitSensorDirector::CollectOverlaps() const
    {
        // TODO: 組を総当たりで見ている。センサーが数十を超えたら格子で絞る
        std::vector<SensorPair> pairs;
        for (std::size_t i = 0; i < m_sensors.size(); ++i)
        {
            HitSensor* first = m_sensors[i];
            if (!first->IsValid())
            {
                continue;
            }
            const SensorVolume firstVolume = VolumeOf(first);
            for (std::size_t j = i + 1; j < m_sensors.size(); ++j)
            {
                HitSensor* second = m_sensors[j];
                if (second->Host() == first->Host() || !second->IsValid())
                {
                    continue;
                }
                if (VolumesOverlap(firstVolume, VolumeOf(second)))
                {
                    pairs.emplace_back(first, second);
                    pairs.emplace_back(second, first);
                }
            }
        }
        return pairs;
    }

    void HitSensorDirector::NotifyOverlaps(const std::vector<SensorPair>& current)
    {
        // 知らせる途中の Unregister も m_overlaps を直すので、前の組は手元へ移してから比べる
        const std::vector<SensorPair> previous = std::exchange(m_overlaps, {});
        const std::vector<SensorPair> pendingExits = std::exchange(m_pendingExits, {});
        for (const SensorPair& pair : pendingExits)
        {
            NotifyExit(pair);
        }
        for (const SensorPair& pair : previous)
        {
            if (!ContainsPair(current, pair))
            {
                NotifyExit(pair);
            }
        }

        for (const SensorPair& pair : current)
        {
            const bool wasOverlapping = ContainsPair(previous, pair);
            if (!IsPresent(pair.first) || !IsPresent(pair.second))
            {
                // 知らせる途中で消えた組。始まりを知らせていた組だけを終わらせる
                if (wasOverlapping)
                {
                    NotifyExit(pair);
                }
                continue;
            }
            SceneActor* owner = pair.first->Host();
            if (owner == nullptr)
            {
                continue;
            }
            m_overlaps.push_back(pair);
            if (wasOverlapping)
            {
                owner->OnSensorStay(pair.first, pair.second);
            }
            else
            {
                owner->OnSensorEnter(pair.first, pair.second);
            }
        }
    }

    void HitSensorDirector::NotifyExit(const SensorPair& pair)
    {
        if (!IsRegistered(pair.first))
        {
            return;
        }
        SceneActor* owner = pair.first->Host();
        if (owner == nullptr)
        {
            return;
        }
        // 知らせる途中で相手が外れていれば、その番地は渡さない
        HitSensor* other = pair.second;
        if (other != nullptr && !IsRegistered(other))
        {
            other = nullptr;
        }
        owner->OnSensorExit(pair.first, other);
    }

    std::vector<HitSensor*> HitSensorDirector::FindOverlaps(const SensorVolume& volume, const SceneActor* ignore) const
    {
        // TODO: 総当たりで見ている。センサーが数十を超えたら格子で絞る
        std::vector<HitSensor*> found;
        for (HitSensor* sensor : m_sensors)
        {
            if (!sensor->IsValid() || sensor->Host() == ignore)
            {
                continue;
            }
            if (VolumesOverlap(volume, VolumeOf(sensor)))
            {
                found.push_back(sensor);
            }
        }
        return found;
    }

    SensorVolume HitSensorDirector::VolumeOf(const HitSensor* sensor) noexcept
    {
        NS_ASSERT(Scene, sensor != nullptr, "VolumeOf: センサーが null");
        NS_ASSERT(Scene, sensor->Host() != nullptr, "VolumeOf: センサーの持ち主が null");
        return sensor->WorldVolume(sensor->Host()->Root().WorldMatrix());
    }
} // namespace NS
