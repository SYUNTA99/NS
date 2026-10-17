#include "NSlib/CoreObject/SubObjects/EffectKeeper.h"
#include "NSlib/Core/Types.h"

#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Gfx/EffectScene.h"

#include <utility>

namespace NS
{
    Gfx::EffectScene* EffectKeeper::Effects() const noexcept
    {
        if (m_owner == nullptr)
        {
            return nullptr;
        }
        const Scene* scene = m_owner->OwningScene();
        if (scene == nullptr)
        {
            return nullptr;
        }
        return scene->GetEffectScene();
    }

    void EffectKeeper::BeginStep()
    {
        Gfx::EffectScene* effects = Effects();
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
        if (Gfx::EffectScene* effects = Effects())
        {
            static_cast<void>(effects->Preload(name));
        }
    }

    uint32 EffectKeeper::Play(std::string_view name, const Gfx::EffectPlayDesc& desc)
    {
        EffectRecord record;
        record.id = m_nextId;
        ++m_nextId;
        record.name = std::string{name};
        record.startStep = m_step;
        if (Gfx::EffectScene* effects = Effects())
        {
            record.handle = effects->Play(name, desc);
        }
        m_records.push_back(std::move(record));
        return m_records.back().id;
    }

    void EffectKeeper::StopRoot(uint32 id) noexcept
    {
        EffectRecord* record = FindMutable(id);
        if (record == nullptr || record->rootStopStep.has_value() || record->endStep.has_value())
        {
            return;
        }
        record->rootStopStep = m_step;
        if (Gfx::EffectScene* effects = Effects())
        {
            effects->StopRoot(record->handle);
        }
    }

    void EffectKeeper::Stop(uint32 id) noexcept
    {
        EffectRecord* record = FindMutable(id);
        if (record == nullptr || record->endStep.has_value())
        {
            return;
        }
        record->endStep = m_step;
        if (Gfx::EffectScene* effects = Effects())
        {
            effects->Stop(record->handle);
        }
    }

    void EffectKeeper::StopAfter(uint32 id, int steps)
    {
        m_scheduledStops.push_back(ScheduledStop{.id = id, .step = m_step + steps});
    }

    void EffectKeeper::SetTransform(uint32 id,
                                    const Vector3& position,
                                    const Quaternion& rotation,
                                    const Vector3& scale) noexcept
    {
        const EffectRecord* record = Find(id);
        Gfx::EffectScene* effects = Effects();
        if (record == nullptr || effects == nullptr)
        {
            return;
        }
        effects->SetTransform(record->handle, position, rotation, scale);
    }

    void EffectKeeper::SetDynamicInput(uint32 id, int index, float value) noexcept
    {
        const EffectRecord* record = Find(id);
        Gfx::EffectScene* effects = Effects();
        if (record == nullptr || effects == nullptr)
        {
            return;
        }
        effects->SetDynamicInput(record->handle, index, value);
    }

    void EffectKeeper::SetAmount(uint32 id, float amount) noexcept
    {
        EffectRecord* record = FindMutable(id);
        if (record == nullptr)
        {
            return;
        }
        record->amount = amount;
    }

    void EffectKeeper::SetRotation(uint32 id, const Quaternion& rotation) noexcept
    {
        EffectRecord* record = FindMutable(id);
        if (record == nullptr)
        {
            return;
        }
        record->rotation = rotation;
    }

    const EffectRecord* EffectKeeper::Find(uint32 id) const noexcept
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

    EffectRecord* EffectKeeper::FindMutable(uint32 id) noexcept
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
        const Gfx::EffectScene* effects = Effects();
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
