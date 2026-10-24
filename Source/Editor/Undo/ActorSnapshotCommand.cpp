#include "Editor/Undo/ActorSnapshotCommand.h"
#include "NSlib/Core/Types.h"

#include "Editor/Undo/IActorSnapshotApplier.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace NS
{
    namespace Editor
    {
        namespace
        {
            //! JSON 木の heap 量を概算する。要素そのものと文字列の確保分を数える
            std::size_t EstimatedJsonBytes(const nlohmann::json& value) noexcept
            {
                if (value.is_string())
                {
                    return value.get_ref<const std::string&>().size();
                }
                std::size_t bytes = 0;
                if (value.is_object())
                {
                    for (nlohmann::json::const_iterator it = value.begin(); it != value.end(); ++it)
                    {
                        bytes += sizeof(nlohmann::json) + it.key().size() + EstimatedJsonBytes(it.value());
                    }
                }
                else if (value.is_array())
                {
                    for (const nlohmann::json& element : value)
                    {
                        bytes += sizeof(nlohmann::json) + EstimatedJsonBytes(element);
                    }
                }
                return bytes;
            }
        } // namespace

        ActorSnapshotCommand::ActorSnapshotCommand(uint32 id,
                                                     std::optional<nlohmann::json> before,
                                                     std::optional<nlohmann::json> after) noexcept
            : m_id(id), m_before(std::move(before)), m_after(std::move(after))
        {}

        ActorSnapshotCommand::~ActorSnapshotCommand()
        {
            ReleaseHeldActors();
        }

        void ActorSnapshotCommand::ReleaseHeldActors() noexcept
        {
            if (m_detached.actor)
            {
                m_detached.actor->OnEndPlay();
                m_detached = DetachedActor{};
            }
            for (std::unique_ptr<SceneActor>& replaced : m_replaced)
            {
                replaced->OnEndPlay();
            }
            m_replaced.clear();
        }

        void ActorSnapshotCommand::Do(IActorSnapshotApplier& target) noexcept
        {
            Apply(target, m_after);
        }

        void ActorSnapshotCommand::Undo(IActorSnapshotApplier& target) noexcept
        {
            Apply(target, m_before);
        }

        void ActorSnapshotCommand::Apply(IActorSnapshotApplier& target,
                                         const std::optional<nlohmann::json>& desired) noexcept
        {
            if (!desired)
            {
                m_detached = target.DetachActor(m_id);
                return;
            }
            // 外したアクターは外した時の姿のまま持っているので、JSON から作り直さずにそのまま戻す
            if (m_detached.actor)
            {
                target.ReattachActor(std::move(m_detached));
                m_detached = DetachedActor{};
                return;
            }
            std::unique_ptr<SceneActor> replaced = target.ApplyActorSnapshot(m_id, *desired);
            if (replaced)
            {
                m_replaced.push_back(std::move(replaced));
            }
        }

        std::size_t ActorSnapshotCommand::EstimatedBytes() const noexcept
        {
            std::size_t bytes = sizeof(ActorSnapshotCommand);
            if (m_before)
            {
                bytes += EstimatedJsonBytes(*m_before);
            }
            if (m_after)
            {
                bytes += EstimatedJsonBytes(*m_after);
            }
            return bytes;
        }
    } // namespace Editor
} // namespace NS
