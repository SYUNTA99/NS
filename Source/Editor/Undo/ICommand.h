#pragma once

#include "NSlib/Core/NonCopyable.h"

#include <cstddef>

namespace NS
{
    namespace Editor
    {
        class IObjectSnapshotApplier;

        //! スナップショットを適用・取消する編集コマンド。描画資源は保持しない
        //! 履歴のメモリ制限のため EstimatedBytes で推定バイト数を返す
        class ICommand : public NS::NonCopyable
        {
        public:
            virtual ~ICommand() = default;

            virtual void Do(IObjectSnapshotApplier& target) noexcept = 0;
            virtual void Undo(IObjectSnapshotApplier& target) noexcept = 0;

            [[nodiscard]] virtual std::size_t EstimatedBytes() const noexcept = 0;

        protected:
            ICommand() = default;
        };
    } // namespace Editor
} // namespace NS
