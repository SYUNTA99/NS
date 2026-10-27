#pragma once

#include "NSlib/Core/NonCopyable.h"

#include <cstddef>

namespace NS
{
    namespace Editor
    {
        class IActorSnapshotApplier;

        //! スナップショットを適用・取消する編集コマンド。描画資源は保持しない
        //! 履歴のメモリ制限のため EstimatedBytes で推定バイト数を返す
        class ICommand : public NonCopyable
        {
        public:
            virtual ~ICommand() = default;

            virtual void Do(IActorSnapshotApplier& target) noexcept = 0;
            virtual void Undo(IActorSnapshotApplier& target) noexcept = 0;

            [[nodiscard]] virtual std::size_t EstimatedBytes() const noexcept = 0;

            //! シーンから外して持っているアクターを、OnEndPlay を呼んでから解放する。前後の JSON は残す
            //! シーンを全部作り直す前に呼ぶ。作り直しは管理クラスを捨てるので、持ったままだと捨てた物を指す
            virtual void ReleaseHeldActors() noexcept {}

        protected:
            ICommand() = default;
        };
    } // namespace Editor
} // namespace NS
