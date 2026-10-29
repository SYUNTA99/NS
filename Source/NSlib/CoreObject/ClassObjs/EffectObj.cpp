#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Gfx/Render/EffectScene.h"

#include <utility>

namespace NS
{
    void EffectObj::BeginStep(Gfx::EffectScene* effects)
    {
        // 前のフレームの終わりの更新で消えた記録。そのフレームの終わりに読んだ時点で残っていない
        if (effects != nullptr)
        {
            for (EffectRecord& record : m_records)
            {
                if (record.endStep.has_value() || !record.handle.IsValid())
                {
                    continue;
                }
                if (!effects->Exists(record.handle))
                {
                    record.endStep = m_step;
                }
            }
        }
        ++m_step;
        const int step = m_step;
        std::erase_if(m_records, [step, this](const EffectRecord& record) {
            return record.endStep.has_value() && step - record.endStep.value() > m_keepEndedSteps;
        });
        for (const ScheduledStop& scheduled : m_scheduledStops)
        {
            if (step >= scheduled.step)
            {
                Stop(effects, scheduled.id);
            }
        }
        std::erase_if(m_scheduledStops, [step](const ScheduledStop& scheduled) { return step >= scheduled.step; });
    }

    uint32 EffectObj::PlayResource(Gfx::EffectScene* effects,
                                const Gfx::EffectResourceDesc& asset,
                                const Gfx::EffectPlayDesc& desc)
    {
        EffectRecord record;
        record.id = m_nextId;
        ++m_nextId;
        record.name = asset.name;
        // ファイルの見つからない識別子は名前を持たないので、記録には頼まれた識別子を残す
        if (record.name.empty())
        {
            record.name = asset.id;
        }
        record.assetId = asset.id;
        record.startStep = m_step;
        if (effects != nullptr)
        {
            record.handle = effects->Play(asset, desc);
        }
        m_records.push_back(std::move(record));
        return m_records.back().id;
    }

    void EffectObj::Preload(Gfx::EffectScene* effects, const Effect* asset)
    {
        if (effects != nullptr && asset != nullptr && asset->IsValid())
        {
            static_cast<void>(effects->Preload(asset->Resource()));
        }
    }

    uint32 EffectObj::Play(Gfx::EffectScene* effects, const Effect* asset, const Gfx::EffectPlayDesc& desc)
    {
        if (asset == nullptr || asset->Id().empty())
        {
            return 0;
        }
        Gfx::EffectScene* destination = nullptr;
        if (asset->IsValid())
        {
            destination = effects;
        }
        return PlayResource(destination, asset->Resource(), desc);
    }

    void EffectObj::StopRoot(Gfx::EffectScene* effects, uint32 id) noexcept
    {
        EffectRecord* record = FindMutable(id);
        if (record == nullptr || record->rootStopStep.has_value() || record->endStep.has_value())
        {
            return;
        }
        record->rootStopStep = m_step;
        if (effects != nullptr)
        {
            effects->StopRoot(record->handle);
        }
    }

    void EffectObj::Stop(Gfx::EffectScene* effects, uint32 id) noexcept
    {
        EffectRecord* record = FindMutable(id);
        if (record == nullptr || record->endStep.has_value())
        {
            return;
        }
        record->endStep = m_step;
        if (effects != nullptr)
        {
            effects->Stop(record->handle);
        }
    }

    void EffectObj::StopAfter(uint32 id, int steps)
    {
        m_scheduledStops.push_back(ScheduledStop{.id = id, .step = m_step + steps});
    }

    void EffectObj::SetTransform(Gfx::EffectScene* effects,
                              uint32 id,
                              const Vector3& position,
                              const Quaternion& rotation,
                              const Vector3& scale) noexcept
    {
        const EffectRecord* record = Find(id);
        if (record == nullptr || effects == nullptr)
        {
            return;
        }
        effects->SetTransform(record->handle, position, rotation, scale);
    }

    void EffectObj::SetDynamicInput(Gfx::EffectScene* effects, uint32 id, int index, float value) noexcept
    {
        const EffectRecord* record = Find(id);
        if (record == nullptr || effects == nullptr)
        {
            return;
        }
        effects->SetDynamicInput(record->handle, index, value);
    }

    void EffectObj::SetAmount(uint32 id, float amount) noexcept
    {
        EffectRecord* record = FindMutable(id);
        if (record == nullptr)
        {
            return;
        }
        record->amount = amount;
    }

    void EffectObj::SetRotation(uint32 id, const Quaternion& rotation) noexcept
    {
        EffectRecord* record = FindMutable(id);
        if (record == nullptr)
        {
            return;
        }
        record->rotation = rotation;
    }

    const EffectRecord* EffectObj::Find(uint32 id) const noexcept
    {
        for (const EffectRecord& record : m_records)
        {
            if (record.id == id)
            {
                return &record;
            }
        }
        return nullptr;
    }

    EffectRecord* EffectObj::FindMutable(uint32 id) noexcept
    {
        return const_cast<EffectRecord*>(std::as_const(*this).Find(id));
    }

    void EffectObj::AppendStartedNames(std::vector<std::string>& out) const
    {
        for (const EffectRecord& record : m_records)
        {
            if (record.startStep == m_step)
            {
                out.push_back(record.name);
            }
        }
    }

    void EffectObj::AppendStartedAmounts(std::vector<std::optional<float>>& out) const
    {
        for (const EffectRecord& record : m_records)
        {
            if (record.startStep == m_step)
            {
                out.push_back(record.amount);
            }
        }
    }

    void EffectObj::AppendLiveNames(const Gfx::EffectScene* effects, std::vector<std::string>& out) const
    {
        for (const EffectRecord& record : m_records)
        {
            if (record.endStep.has_value())
            {
                continue;
            }
            // 再生していない記録は Effekseer に問えないので、Stop までを残っている間とする
            if (effects != nullptr && record.handle.IsValid() && !effects->Exists(record.handle))
            {
                continue;
            }
            out.push_back(record.name);
        }
    }
} // namespace NS
