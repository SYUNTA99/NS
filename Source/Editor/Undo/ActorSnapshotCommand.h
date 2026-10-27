#pragma once

#include "Editor/Undo/ICommand.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/ActorList.h"

#include <memory>
#include <optional>
#include <vector>

namespace NS
{
    namespace Editor
    {
        //! 永続 id で対象を引き、JSON の before・after を切り替える
        class ActorSnapshotCommand final : public ICommand
        {
        public:
            //! before は Undo、after は実行・Redo の状態。nullopt は個体が存在しない状態を表す
            ActorSnapshotCommand(uint32 id,
                                  std::optional<nlohmann::json> before,
                                  std::optional<nlohmann::json> after) noexcept;

            ~ActorSnapshotCommand() override;

            void Do(IActorSnapshotApplier& target) noexcept override;
            void Undo(IActorSnapshotApplier& target) noexcept override;

            [[nodiscard]] std::size_t EstimatedBytes() const noexcept override;

            void ReleaseHeldActors() noexcept override;

        private:
            void Apply(IActorSnapshotApplier& target, const std::optional<nlohmann::json>& desired) noexcept;

            uint32 m_id;
            std::optional<nlohmann::json> m_before;
            std::optional<nlohmann::json> m_after;
            DetachedActor m_detached;
            std::vector<std::unique_ptr<SceneActor>> m_replaced;
        };
    } // namespace Editor
} // namespace NS
