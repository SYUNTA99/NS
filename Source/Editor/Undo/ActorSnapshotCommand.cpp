#include "Editor/Undo/ActorSnapshotCommand.h"
#include "NSlib/Core/Types.h"

#include "Editor/Undo/IActorSnapshotApplier.h"

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

        void ActorSnapshotCommand::Do(IActorSnapshotApplier& target) noexcept
        {
            target.ApplyActorSnapshot(m_id, m_after);
        }

        void ActorSnapshotCommand::Undo(IActorSnapshotApplier& target) noexcept
        {
            target.ApplyActorSnapshot(m_id, m_before);
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
