#include "Editor/Undo/ObjectSnapshotCommand.h"

#include "Editor/Undo/IObjectSnapshotApplier.h"

namespace NS::Editor
{
    namespace
    {
        /// JSON 木の heap 量を概算する。 要素そのものと文字列の確保分を数える
        std::size_t EstimatedJsonBytes(const nlohmann::json& value) noexcept
        {
            if (value.is_string())
                return value.get_ref<const std::string&>().size();
            std::size_t bytes = 0;
            if (value.is_object())
            {
                for (auto it = value.begin(); it != value.end(); ++it)
                    bytes += sizeof(nlohmann::json) + it.key().size() + EstimatedJsonBytes(it.value());
            }
            else if (value.is_array())
            {
                for (const nlohmann::json& element : value)
                    bytes += sizeof(nlohmann::json) + EstimatedJsonBytes(element);
            }
            return bytes;
        }

        /// sizeof 外の heap 量。 components の JSON 木を再帰で概算する
        std::size_t EstimatedHeapBytes(const NS::Object::ObjectData& object) noexcept
        {
            return object.className.capacity() + EstimatedJsonBytes(object.components);
        }
    } // namespace

    ObjectSnapshotCommand::ObjectSnapshotCommand(std::uint32_t id,
                                                 std::optional<NS::Object::ObjectData> before,
                                                 std::optional<NS::Object::ObjectData> after) noexcept
        : m_id(id), m_before(std::move(before)), m_after(std::move(after))
    {}

    void ObjectSnapshotCommand::Do(IObjectSnapshotApplier& target) noexcept
    {
        target.ApplyObjectSnapshot(m_id, m_after);
    }

    void ObjectSnapshotCommand::Undo(IObjectSnapshotApplier& target) noexcept
    {
        target.ApplyObjectSnapshot(m_id, m_before);
    }

    std::size_t ObjectSnapshotCommand::EstimatedBytes() const noexcept
    {
        std::size_t bytes = sizeof(ObjectSnapshotCommand);
        if (m_before)
            bytes += EstimatedHeapBytes(*m_before);
        if (m_after)
            bytes += EstimatedHeapBytes(*m_after);
        return bytes;
    }

} // namespace NS::Editor
