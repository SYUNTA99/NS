#include "NSlib/CoreObject/ClassObjs/SoundObj.h"

#include <utility>

namespace NS
{
    uint32 SoundObj::PlayLoop(const Audio::AudioDevice* device, const Sound* sound, float volume)
    {
        if (sound == nullptr || sound->Path().empty())
        {
            return 0;
        }
        SoundRecord record;
        record.asset = sound;
        record.volume = volume;
        if (device != nullptr && sound->IsValid())
        {
            record.loop = std::make_unique<Audio::SoundLoop>(*sound->XA2());
            record.loop->SetVolume(volume);
            record.loop->Play();
        }
        record.id = m_nextId;
        ++m_nextId;
        m_records.push_back(std::move(record));
        return m_records.back().id;
    }

    void SoundObj::Stop(uint32 id) noexcept
    {
        const std::size_t index = IndexOf(id);
        if (index == m_records.size())
        {
            return;
        }
        // ソースボイスは SoundLoop の破棄で止まる
        m_records.erase(m_records.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void SoundObj::StopAll() noexcept
    {
        m_records.clear();
    }

    void SoundObj::SetVolume(uint32 id, float volume) noexcept
    {
        const std::size_t index = IndexOf(id);
        if (index == m_records.size())
        {
            return;
        }
        m_records[index].volume = volume;
        if (m_records[index].loop)
        {
            m_records[index].loop->SetVolume(volume);
        }
    }

    void SoundObj::Update()
    {
        for (const SoundRecord& record : m_records)
        {
            if (record.loop)
            {
                record.loop->Play();
            }
        }
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
