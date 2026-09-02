#pragma once

#include "Editor/Undo/ICommand.h"

#include <memory>
#include <vector>

namespace NS::Editor
{

    //! @brief 複数のコマンドを 1 回の undo で往復させる入れ物
    //! @details Do は積んだ順、 Undo は逆順に流す
    //! 親ごと子孫をまとめて消すような 1 操作 = 複数オブジェクトの編集で使う
    class CompositeCommand final : public ICommand
    {
    public:
        explicit CompositeCommand(std::vector<std::unique_ptr<ICommand>> commands) noexcept;

        void Do(IObjectSnapshotApplier& target) noexcept override;
        void Undo(IObjectSnapshotApplier& target) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override;

    private:
        std::vector<std::unique_ptr<ICommand>> m_commands; // 積んだ順に Do する子コマンド
    };

} // namespace NS::Editor
