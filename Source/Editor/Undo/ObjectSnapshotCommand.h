#pragma once

#include "Editor/Undo/ICommand.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ObjectJson.h"

#include <optional>

namespace NS
{
    namespace Editor
    {
        //! 永続 id で対象を引き、JSON の before・after を切り替える。派生状態の同期は適用側が行う
        class ObjectSnapshotCommand final : public ICommand
        {
        public:
            //! before は Undo、after は実行・Redo の状態。nullopt は個体が存在しない状態
            ObjectSnapshotCommand(uint32 id,
                                  std::optional<nlohmann::json> before,
                                  std::optional<nlohmann::json> after) noexcept;

            void Do(IObjectSnapshotApplier& target) noexcept override;
            void Undo(IObjectSnapshotApplier& target) noexcept override;

            [[nodiscard]] std::size_t EstimatedBytes() const noexcept override;

        private:
            uint32 m_id;                        // 対象オブジェクトの永続 id
            std::optional<nlohmann::json> m_before; // 適用前の姿 (Undo 復元用、無ければ未存在)
            std::optional<nlohmann::json> m_after;  // 適用後の姿 (無ければ除去)
        };
    } // namespace Editor
} // namespace NS
