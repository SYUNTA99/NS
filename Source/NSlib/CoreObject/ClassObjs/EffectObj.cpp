#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Types.h"

#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IGet/IGetEffect.h"
#include "NSlib/Gfx/Render/EffectScene.h"

#include <utility>

namespace NS
{
    EffectObj::EffectObj(const IGetEffect* user, const Transform* transform) noexcept
        : m_user(user), m_transform(transform)
    {
        NS_ASSERT(Scene, user != nullptr, "EffectObj に null の IGetEffect を渡している");
        NS_ASSERT(Scene, transform != nullptr, "EffectObj に null の Transform を渡している");
    }

    uint32 EffectObj::Play(const Effect* asset, const Vector3& position)
    {
        return Start(asset, Gfx::EffectPlayDesc{.position = position}, false);
    }

    uint32 EffectObj::PlayFollowing(const Effect* asset)
    {
        return Start(asset,
                     Gfx::EffectPlayDesc{.position = m_transform->Position(), .rotation = m_transform->Rotation()},
                     true);
    }

    uint32 EffectObj::Start(const Effect* asset, const Gfx::EffectPlayDesc& desc, bool follows)
    {
        if (asset == nullptr || asset->Path().empty())
        {
            return 0;
        }
        PlayingEffect record;
        record.asset = asset;
        record.follows = follows;
        if (Gfx::EffectScene* effects = m_user->GetEffectScene())
        {
            record.handle = effects->Play(asset->D3D(), asset->Path(), desc);
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

    void EffectObj::StopRoot(uint32 id) noexcept
    {
        const Gfx::EffectHandle handle = TakeOut(id);
        if (Gfx::EffectScene* effects = m_user->GetEffectScene())
        {
            effects->StopRoot(handle);
        }
    }

    void EffectObj::Stop(uint32 id) noexcept
    {
        const Gfx::EffectHandle handle = TakeOut(id);
        if (Gfx::EffectScene* effects = m_user->GetEffectScene())
        {
            effects->Stop(handle);
        }
    }

    void EffectObj::StopRootAll() noexcept
    {
        if (Gfx::EffectScene* effects = m_user->GetEffectScene())
        {
            for (const PlayingEffect& record : m_records)
            {
                effects->StopRoot(record.handle);
            }
        }
        m_records.clear();
    }

    void EffectObj::SetTransform(uint32 id,
                                 const Vector3& position,
                                 const Quaternion& rotation,
                                 const Vector3& scale) noexcept
    {
        const std::size_t index = IndexOf(id);
        Gfx::EffectScene* effects = m_user->GetEffectScene();
        if (index == m_records.size() || effects == nullptr)
        {
            return;
        }
        effects->SetTransform(m_records[index].handle, position, rotation, scale);
    }

    void EffectObj::SetDynamicInput(uint32 id, int index, float value) noexcept
    {
        const std::size_t recordIndex = IndexOf(id);
        Gfx::EffectScene* effects = m_user->GetEffectScene();
        if (recordIndex == m_records.size() || effects == nullptr)
        {
            return;
        }
        effects->SetDynamicInput(m_records[recordIndex].handle, index, value);
    }

    void EffectObj::Update() noexcept
    {
        Gfx::EffectScene* effects = m_user->GetEffectScene();
        if (effects == nullptr)
        {
            // 再生していないので、終わったかを問えない。出すだけの再生は止める呼び出しが来ないので、ここで外す
            std::erase_if(m_records, [](const PlayingEffect& record) { return !record.follows; });
            return;
        }
        std::erase_if(m_records, [effects](const PlayingEffect& record) { return !effects->Exists(record.handle); });
        for (const PlayingEffect& record : m_records)
        {
            if (record.follows)
            {
                effects->SetTransform(record.handle, m_transform->Position(), m_transform->Rotation(), Vector3::One);
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
