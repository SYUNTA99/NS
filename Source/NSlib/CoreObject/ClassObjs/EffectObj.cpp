#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Gfx/Render/EffectScene.h"

#include <utility>

namespace NS
{
    uint32 EffectObj::Play(Gfx::EffectScene* effects, const Effect* asset, const Gfx::EffectPlayDesc& desc)
    {
        return Start(effects, asset, desc, false);
    }

    uint32 EffectObj::PlayFollowing(Gfx::EffectScene* effects, const Effect* asset, const Gfx::EffectPlayDesc& desc)
    {
        return Start(effects, asset, desc, true);
    }

    uint32 EffectObj::Start(Gfx::EffectScene* effects,
                            const Effect* asset,
                            const Gfx::EffectPlayDesc& desc,
                            bool follows)
    {
        if (asset == nullptr || asset->Path().empty())
        {
            return 0;
        }
        EffectRecord record;
        record.asset = asset;
        record.follows = follows;
        record.scale = desc.scale;
        if (effects != nullptr)
        {
            record.handle = effects->Play(asset->D3D(), desc);
            // 読めない素材は再生されない。Effekseer に問えない物を一覧に残すと、外す時が来ない
            if (!record.handle.IsValid())
            {
                return 0;
            }
        }
        record.id = m_nextId;
        ++m_nextId;
        m_records.push_back(std::move(record));
        return m_records.back().id;
    }

    void EffectObj::StopRoot(Gfx::EffectScene* effects, uint32 id) noexcept
    {
        const Gfx::EffectHandle handle = TakeOut(id);
        if (effects != nullptr)
        {
            effects->StopRoot(handle);
        }
    }

    void EffectObj::Stop(Gfx::EffectScene* effects, uint32 id) noexcept
    {
        const Gfx::EffectHandle handle = TakeOut(id);
        if (effects != nullptr)
        {
            effects->Stop(handle);
        }
    }

    void EffectObj::StopRootAll(Gfx::EffectScene* effects) noexcept
    {
        if (effects != nullptr)
        {
            for (const EffectRecord& record : m_records)
            {
                effects->StopRoot(record.handle);
            }
        }
        m_records.clear();
    }

    void EffectObj::SetTransform(Gfx::EffectScene* effects,
                                 uint32 id,
                                 const Vector3& position,
                                 const Quaternion& rotation,
                                 const Vector3& scale) noexcept
    {
        const std::size_t index = IndexOf(id);
        if (index == m_records.size() || effects == nullptr)
        {
            return;
        }
        m_records[index].scale = scale;
        effects->SetTransform(m_records[index].handle, position, rotation, scale);
    }

    void EffectObj::SetDynamicInput(Gfx::EffectScene* effects, uint32 id, int index, float value) noexcept
    {
        const std::size_t recordIndex = IndexOf(id);
        if (recordIndex == m_records.size() || effects == nullptr)
        {
            return;
        }
        effects->SetDynamicInput(m_records[recordIndex].handle, index, value);
    }

    void EffectObj::DropEnded(const Gfx::EffectScene* effects) noexcept
    {
        if (effects == nullptr)
        {
            // 再生していないので、終わったかを問えない。出すだけの再生は止める呼び出しが来ないので、ここで外す
            std::erase_if(m_records, [](const EffectRecord& record) { return !record.follows; });
            return;
        }
        std::erase_if(m_records, [effects](const EffectRecord& record) { return !effects->Exists(record.handle); });
    }

    void EffectObj::Follow(Gfx::EffectScene* effects, const Vector3& position, const Quaternion& rotation) noexcept
    {
        if (effects == nullptr)
        {
            return;
        }
        for (const EffectRecord& record : m_records)
        {
            if (record.follows)
            {
                effects->SetTransform(record.handle, position, rotation, record.scale);
            }
        }
    }

    Gfx::EffectHandle EffectObj::TakeOut(uint32 id) noexcept
    {
        const std::size_t index = IndexOf(id);
        if (index == m_records.size())
        {
            return {};
        }
        const Gfx::EffectHandle handle = m_records[index].handle;
        m_records.erase(m_records.begin() + static_cast<std::ptrdiff_t>(index));
        return handle;
    }

    std::size_t EffectObj::IndexOf(uint32 id) const noexcept
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
