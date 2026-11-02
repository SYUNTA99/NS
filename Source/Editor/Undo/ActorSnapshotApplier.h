#pragma once

#include "Editor/Undo/IActorSnapshotApplier.h"

#include "NSlib/Core/Types.h"
#include <optional>

namespace NS
{
    class Scene;
}

namespace NS
{
    namespace Editor
    {
        class ActorSnapshotApplier final : public IActorSnapshotApplier
        {
        public:
            explicit ActorSnapshotApplier(Scene& scene) noexcept : m_scene(scene) {}

            [[nodiscard]] std::optional<nlohmann::json> CaptureActor(uint32 objectId) const override;

            [[nodiscard]] std::unique_ptr<SceneActor> ApplyActorSnapshot(uint32 objectId,
                                                                         const nlohmann::json& desired) override;

            [[nodiscard]] DetachedActor DetachActor(uint32 objectId) override;

            void ReattachActor(DetachedActor detached) override;

        private:
            Scene& m_scene;
        };
    } // namespace Editor
} // namespace NS
