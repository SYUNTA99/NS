#include "NSlib/CoreObject/ClassObjs/SoundObj.h"

#include <utility>

namespace NS
{
    uint32 SoundObj::Play(Audio::SoundScene* sounds, const Sound* sound, float volume, bool loop)
    {
        return Start(sounds, sound, nullptr, volume, loop);
    }

    uint32 SoundObj::PlayAt(
        Audio::SoundScene* sounds, const Sound* sound, const Vector3& position, float volume, bool loop)
    {
        return Start(sounds, sound, &position, volume, loop);
    }

    uint32 SoundObj::Start(
        Audio::SoundScene* sounds, const Sound* sound, const Vector3* position, float volume, bool loop)
    {
        if (sound == nullptr || sound->Path().empty())
        {
            return 0;
        }
        SoundRecord record;
        record.asset = sound;
        record.loop = loop;
        record.volume = volume;
        if (sounds != nullptr && position != nullptr)
        {
            record.handle = sounds->PlayAt(sound->XA2(), *position, volume, loop);
        }
        else if (sounds != nullptr)
        {
            record.handle = sounds->Play(sound->XA2(), volume, loop);
        }
        record.id = m_nextId;
        ++m_nextId;
        m_records.push_back(std::move(record));
        return m_records.back().id;
    }

    void SoundObj::Stop(Audio::SoundScene* sounds, uint32 id) noexcept
    {
        const std::size_t index = IndexOf(id);
        if (index == m_records.size())
        {
            return;
        }
        if (sounds != nullptr)
        {
            sounds->Stop(m_records[index].handle);
        }
        m_records.erase(m_records.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void SoundObj::StopLoopAll(Audio::SoundScene* sounds) noexcept
    {
        if (sounds != nullptr)
        {
            for (const SoundRecord& record : m_records)
            {
                if (record.loop)
                {
                    sounds->Stop(record.handle);
                }
            }
        }
        m_records.clear();
    }

    void SoundObj::SetVolume(Audio::SoundScene* sounds, uint32 id, float volume)
    {
        const std::size_t index = IndexOf(id);
        if (index == m_records.size())
        {
            return;
        }
        m_records[index].volume = volume;
        if (sounds != nullptr)
        {
            sounds->SetVolume(m_records[index].handle, volume);
        }
    }

    void SoundObj::DropEnded(const Audio::SoundScene* sounds) noexcept
    {
        if (sounds == nullptr)
        {
            // 鳴らしていないので、終わったかを問えない。1 回の音は止める呼び出しが来ないので、ここで外す
            std::erase_if(m_records, [](const SoundRecord& record) { return !record.loop; });
            return;
        }
        std::erase_if(m_records,
                      [sounds](const SoundRecord& record) { return !record.loop && !sounds->Exists(record.handle); });
    }

    std::size_t SoundObj::IndexOf(uint32 id) const noexcept
    {
        for (std::size_t index = 0; index < m_records.size(); ++index)
        {
            if (m_records[index].id == id)
            {
                return index;
            }
        }
        return m_records.size();
    }
} // namespace NS
