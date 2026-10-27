#pragma once

#include "Editor/Undo/ICommand.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/ActorList.h"

#include <memory>
#include <optional>
#include <vector>

namespace NS::Editor
{
        //! 永続 id で対象を引き、JSON の before・after を切り替える。派生状態の同期は適用側が行う
        //! 居ない姿へ切り替える時はアクターを解放せずに外して持ち、居る姿へ戻す時に同じアクターを戻す
        class ActorSnapshotCommand final : public ICommand
        {
        public:
            //! before は Undo、after は実行・Redo の状態。nullopt は個体が存在しない状態
            ActorSnapshotCommand(uint32 id,
                                  std::optional<nlohmann::json> before,
                                  std::optional<nlohmann::json> after) noexcept;

            //! 外して持っているアクターがあれば、OnEndPlay を呼んでから解放する
            ~ActorSnapshotCommand() override;

            void Do(IActorSnapshotApplier& target) noexcept override;
            void Undo(IActorSnapshotApplier& target) noexcept override;

            [[nodiscard]] std::size_t EstimatedBytes() const noexcept override;

            //! 解放した後に居る姿へ戻す時は、JSON から作り直す
            void ReleaseHeldActors() noexcept override;

        private:
            void Apply(IActorSnapshotApplier& target, const std::optional<nlohmann::json>& desired) noexcept;

            uint32 m_id;                        // 対象オブジェクトの永続 id
            std::optional<nlohmann::json> m_before; // 適用前の姿 (Undo 復元用、無ければ未存在)
            std::optional<nlohmann::json> m_after;  // 適用後の姿 (無ければ除去)
            DetachedActor m_detached;               // 居ない姿の間、外して持つアクター
            std::vector<std::unique_ptr<SceneActor>> m_replaced; // 姿を当てた時に作り直して外した古いアクター。戻さない
        };
} // namespace NS::Editor
