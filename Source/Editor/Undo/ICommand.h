#pragma once

#include "Runtime/Core/NonCopyable.h"

#include <cstddef>

namespace NS::Editor
{
    class IObjectSnapshotApplier;

    //! @brief Undo/Redo コマンドの基底
    //!
    //! @note live 実体を唯一の authored 表現とし、 編集は objectId 単位の before/after スナップショットで表す
    //! @note Do/Undo は IObjectSnapshotApplier 越しに 1 体を組み直すだけ。 描画リソースは保持禁止
    //! @note 50MB のメモリ上限管理のため、EstimatedBytes() で使用量を見積もること
    class ICommand : public NS::Core::NonCopyable
    {
    public:
        virtual ~ICommand() = default;

        virtual void Do(IObjectSnapshotApplier& target) noexcept = 0;
        virtual void Undo(IObjectSnapshotApplier& target) noexcept = 0;

        [[nodiscard]] virtual std::size_t EstimatedBytes() const noexcept = 0;

    protected:
        ICommand() = default;
    };

} // namespace NS::Editor
