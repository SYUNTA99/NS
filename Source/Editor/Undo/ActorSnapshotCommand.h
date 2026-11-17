#pragma once

#include "Editor/Undo/ICommand.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Scene/Scene.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace NS
{
    namespace Ed
    {
        //! 永続 id で対象を引き、JSON の before・after を切り替える
        class ActorSnapshotCommand final : public ICommand
        {
        public:
            //! before は Undo、after は実行・Redo の状態。nullopt は個体が存在しない状態を表す
            //! spawnIndex は、居ない姿から JSON で作り直す時に入れる並びの位置。
            //! 消した後に積む時は、消す前の位置を渡す。外したアクターを持って戻す時は使わない
            ActorSnapshotCommand(Scene& scene,
                                  uint32 id,
                                  std::optional<nlohmann::json> before,
                                  std::optional<nlohmann::json> after,
                                  std::size_t spawnIndex = Scene::k_SpawnAtEnd) noexcept;

            ~ActorSnapshotCommand() override;

            void Do() noexcept override;
            void Undo() noexcept override;

            [[nodiscard]] std::size_t EstimatedBytes() const noexcept override;

            void ReleaseHeldActors() noexcept override;

        private:
            void Apply(const std::optional<nlohmann::json>& desired) noexcept;

            Scene& m_scene;
            uint32 m_id;
            std::optional<nlohmann::json> m_before;
            std::optional<nlohmann::json> m_after;
            std::size_t m_spawnIndex = Scene::k_SpawnAtEnd;
            DetachedActor m_detached;
            std::vector<std::unique_ptr<SceneActor>> m_replaced;
        };
    } // namespace Ed
} // namespace NS
