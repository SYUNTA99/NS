#include "Game/Player/EffectLayerList.h"

#include "Game/Level/EffectSwitches.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/SubObject.h"

#include <utility>

namespace GL::Player
{
    void EffectLayerList::BeginStep(NS::Gfx::EffectScene* effects)
    {
        // 前のフレームの終わりの更新で消えた層。そのフレームの終わりに記録を読んだ時点で残っていない
        if (effects != nullptr)
        {
            for (EffectLayerRecord& record : m_records)
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
        std::erase_if(m_records, [step, this](const EffectLayerRecord& record) {
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

    void EffectLayerList::StopAfter(std::uint32_t id, int steps)
    {
        m_scheduledStops.push_back(ScheduledStop{.id = id, .step = m_step + steps});
    }

    std::uint32_t EffectLayerList::Play(NS::Gfx::EffectScene* effects,
                                        std::string_view name,
                                        const NS::Gfx::EffectPlayDesc& desc)
    {
        if (GL::Level::EffectSwitches::Get().IsOff(name))
        {
            return 0;
        }
        EffectLayerRecord record;
        record.id = m_nextId;
        ++m_nextId;
        record.name = std::string{name};
        record.startStep = m_step;
        if (effects != nullptr)
        {
            record.handle = effects->Play(name, desc);
        }
        m_records.push_back(std::move(record));
        return m_records.back().id;
    }

    void EffectLayerList::StopRoot(NS::Gfx::EffectScene* effects, std::uint32_t id) noexcept
    {
        EffectLayerRecord* record = FindMutable(id);
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

    void EffectLayerList::Stop(NS::Gfx::EffectScene* effects, std::uint32_t id) noexcept
    {
        EffectLayerRecord* record = FindMutable(id);
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

    void EffectLayerList::SetAmount(std::uint32_t id, float amount) noexcept
    {
        EffectLayerRecord* record = FindMutable(id);
        if (record == nullptr)
        {
            return;
        }
        record->amount = amount;
    }

    void EffectLayerList::SetRotation(std::uint32_t id, const NS::Quaternion& rotation) noexcept
    {
        EffectLayerRecord* record = FindMutable(id);
        if (record == nullptr)
        {
            return;
        }
        record->rotation = rotation;
    }

    const EffectLayerRecord* EffectLayerList::Find(std::uint32_t id) const noexcept
    {
        for (const EffectLayerRecord& record : m_records)
        {
            if (record.id == id)
            {
                return &record;
            }
        }
        return nullptr;
    }

    EffectLayerRecord* EffectLayerList::FindMutable(std::uint32_t id) noexcept
    {
        return const_cast<EffectLayerRecord*>(std::as_const(*this).Find(id));
    }

    void EffectLayerList::AppendStartedNames(std::vector<std::string>& out) const
    {
        for (const EffectLayerRecord& record : m_records)
        {
            if (record.startStep == m_step)
            {
                out.push_back(record.name);
            }
        }
    }

    void EffectLayerList::AppendStartedAmounts(std::vector<std::optional<float>>& out) const
    {
        for (const EffectLayerRecord& record : m_records)
        {
            if (record.startStep == m_step)
            {
                out.push_back(record.amount);
            }
        }
    }

    void EffectLayerList::AppendLiveNames(const NS::Gfx::EffectScene* effects, std::vector<std::string>& out) const
    {
        for (const EffectLayerRecord& record : m_records)
        {
            if (record.endStep.has_value())
            {
                continue;
            }
            // 再生していない層は Effekseer に問えないので、Stop までを残っている間とする
            if (effects != nullptr && record.handle.IsValid() && !effects->Exists(record.handle))
            {
                continue;
            }
            out.push_back(record.name);
        }
    }

    void PreloadLayers(NS::Gfx::EffectScene* effects, std::initializer_list<std::string_view> names)
    {
        for (const std::string_view name : names)
        {
            GL::Level::EffectSwitches::Get().AddLayerName(name);
            // 読めない絵は Play が無効なハンドルを返し、記録だけ残る。警告は EffectScene が名前ごとに 1 回出す
            if (effects != nullptr)
            {
                static_cast<void>(effects->Preload(name));
            }
        }
    }

    NS::Gfx::EffectScene* EffectsOf(const NS::Obj::SubObject& subObject) noexcept
    {
        // エフェクトの窓口から引く。持ち主がシーンに居なければ窓口が nullptr を返す
        const NS::Obj::Actor* owner = subObject.Owner();
        if (owner == nullptr)
        {
            return nullptr;
        }
        return owner->GetEffectScene();
    }
} // namespace GL::Player
