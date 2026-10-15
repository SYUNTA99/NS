#include "NSlib/CoreObject/SubObjects/EffectKeeper.h"
#include "NSlib/Core/Types.h"

#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Graphics/EffectScene.h"

#include <utility>

namespace NS
{
    NS::Gfx::EffectScene* EffectKeeper::Effects() const noexcept
    {
        const Scene* scene = OwningScene();
        if (scene == nullptr)
        {
            return nullptr;
        }
        return scene->GetEffectScene();
    }

    void EffectKeeper::BeginStep()
    {
        NS::Gfx::EffectScene* effects = Effects();
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
                Stop(scheduled.id);
            }
        }
        std::erase_if(m_scheduledStops, [step](const ScheduledStop& scheduled) { return step >= scheduled.step; });
    }

    void EffectKeeper::Preload(std::string_view name)
    {
        // 読めない絵は Play が無効なハンドルを返し、記録だけ残る。警告は EffectScene が名前ごとに 1 回出す
        if (NS::Gfx::EffectScene* effects = Effects())
        {
            static_cast<void>(effects->Preload(name));
        }
    }

    NS::uint32 EffectKeeper::Play(std::string_view name, const NS::Gfx::EffectPlayDesc& desc)
    {
        EffectRecord record;
        record.id = m_nextId;
        ++m_nextId;
        record.name = std::string{name};
        record.startStep = m_step;
        if (NS::Gfx::EffectScene* effects = Effects())
        {
            record.handle = effects->Play(name, desc);
        }
        m_records.push_back(std::move(record));
        return m_records.back().id;
    }

    void EffectKeeper::StopRoot(NS::uint32 id) noexcept
    {
        EffectRecord* record = FindMutable(id);
        if (record == nullptr || record->rootStopStep.has_value() || record->endStep.has_value())
        {
            return;
        }
        record->rootStopStep = m_step;
        if (NS::Gfx::EffectScene* effects = Effects())
        {
            effects->StopRoot(record->handle);
        }
    }

    void EffectKeeper::Stop(NS::uint32 id) noexcept
    {
        EffectRecord* record = FindMutable(id);
        if (record == nullptr || record->endStep.has_value())
        {
            return;
        }
        record->endStep = m_step;
        if (NS::Gfx::EffectScene* effects = Effects())
        {
            effects->Stop(record->handle);
        }
    }

    void EffectKeeper::StopAfter(NS::uint32 id, int steps)
    {
        m_scheduledStops.push_back(ScheduledStop{.id = id, .step = m_step + steps});
    }

    void EffectKeeper::SetTransform(NS::uint32 id,
                                    const NS::Vector3& position,
                                    const NS::Quaternion& rotation,
                                    const NS::Vector3& scale) noexcept
    {
        const EffectRecord* record = Find(id);
        NS::Gfx::EffectScene* effects = Effects();
        if (record == nullptr || effects == nullptr)
        {
            return;
        }
        effects->SetTransform(record->handle, position, rotation, scale);
    }

    void EffectKeeper::SetDynamicInput(NS::uint32 id, int index, float value) noexcept
    {
        const EffectRecord* record = Find(id);
        NS::Gfx::EffectScene* effects = Effects();
        if (record == nullptr || effects == nullptr)
        {
            return;
        }
        effects->SetDynamicInput(record->handle, index, value);
    }

    void EffectKeeper::SetAmount(NS::uint32 id, float amount) noexcept
    {
        EffectRecord* record = FindMutable(id);
        if (record == nullptr)
        {
            return;
        }
        record->amount = amount;
    }

    void EffectKeeper::SetRotation(NS::uint32 id, const NS::Quaternion& rotation) noexcept
    {
        EffectRecord* record = FindMutable(id);
        if (record == nullptr)
        {
            return;
        }
        record->rotation = rotation;
    }

    const EffectRecord* EffectKeeper::Find(NS::uint32 id) const noexcept
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

    EffectRecord* EffectKeeper::FindMutable(NS::uint32 id) noexcept
    {
        return const_cast<EffectRecord*>(std::as_const(*this).Find(id));
    }

    void EffectKeeper::AppendStartedNames(std::vector<std::string>& out) const
    {
        for (const EffectRecord& record : m_records)
        {
            if (record.startStep == m_step)
            {
                out.push_back(record.name);
            }
        }
    }

    void EffectKeeper::AppendStartedAmounts(std::vector<std::optional<float>>& out) const
    {
        for (const EffectRecord& record : m_records)
        {
            if (record.startStep == m_step)
            {
                out.push_back(record.amount);
            }
        }
    }

    void EffectKeeper::AppendLiveNames(std::vector<std::string>& out) const
    {
        const NS::Gfx::EffectScene* effects = Effects();
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
