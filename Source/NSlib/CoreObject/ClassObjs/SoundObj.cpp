#include "NSlib/CoreObject/ClassObjs/SoundObj.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/IGet/IGetAudio.h"

#include <utility>

namespace NS
{
    SoundObj::SoundObj(const IGetAudio* user) noexcept : m_user(user)
    {
        NS_ASSERT(Scene, user != nullptr, "SoundObj に null の IGetAudio を渡している");
    }

    uint32 SoundObj::Play(const Sound* sound, float volume, bool loop)
    {
        return Start(sound, nullptr, volume, loop);
    }

    uint32 SoundObj::PlayAt(const Sound* sound, const Vector3& position, float volume, bool loop)
    {
        return Start(sound, &position, volume, loop);
    }

    uint32 SoundObj::Start(const Sound* sound, const Vector3* position, float volume, bool loop)
    {
        if (sound == nullptr || sound->Path().empty())
        {
            return 0;
        }
        PlayingSound record;
        record.loop = loop;
        Audio::SoundScene* sounds = m_user->GetSoundScene();
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

    void SoundObj::Stop(uint32 id) noexcept
    {
        const std::size_t index = IndexOf(id);
        if (index == m_records.size())
        {
            return;
        }
        if (Audio::SoundScene* sounds = m_user->GetSoundScene())
        {
            sounds->Stop(m_records[index].handle);
        }
        m_records.erase(m_records.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void SoundObj::StopLoopAll() noexcept
    {
        if (Audio::SoundScene* sounds = m_user->GetSoundScene())
        {
            for (const PlayingSound& record : m_records)
            {
                if (record.loop)
                {
                    sounds->Stop(record.handle);
                }
            }
        }
        m_records.clear();
    }

    void SoundObj::SetVolume(uint32 id, float volume)
    {
        const std::size_t index = IndexOf(id);
        if (index == m_records.size())
        {
            return;
        }
        if (Audio::SoundScene* sounds = m_user->GetSoundScene())
        {
            sounds->SetVolume(m_records[index].handle, volume);
        }
    }

    void SoundObj::Update() noexcept
    {
        const Audio::SoundScene* sounds = m_user->GetSoundScene();
        if (sounds == nullptr)
        {
            // 鳴らしていないので、終わったかを問えない。1 回の音は止める呼び出しが来ないので、ここで外す
            std::erase_if(m_records, [](const PlayingSound& record) { return !record.loop; });
            return;
        }
        std::erase_if(m_records,
                      [sounds](const PlayingSound& record) { return !record.loop && !sounds->Exists(record.handle); });
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
