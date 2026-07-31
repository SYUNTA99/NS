#pragma once

#include "Runtime/Core/NonCopyable.h"

#include <string>
#include <string_view>
#include <vector>

namespace NS::Editor
{
    //! @brief レベルファイルの保存・読込を行うためのモーダルUI
    class LevelFileBrowser : public NS::Core::NonCopyable
    {
    public:
        enum class Action : std::uint8_t
        {
            None,
            RequestSave,
            RequestLoad,
        };

        struct Result
        {
            Action action = Action::None;
            std::string targetName;
        };

        LevelFileBrowser() = default;

        //! @brief 保存用モーダルを開く
        void OpenSaveModal(std::string_view initialName = {}) noexcept;

        //! @brief 読込用モーダルを開く
        void OpenLoadModal() noexcept;

        //! @brief UIの描画処理を行う。ユーザーの操作結果をResultとして返す
        [[nodiscard]] Result Render() noexcept;

        //! @brief 保存結果のフィードバックを表示する
        void NotifySaveResult(bool ok, std::string_view message) noexcept;

        //! @brief 読込結果のフィードバックを表示する
        void NotifyLoadResult(bool ok, std::string_view message) noexcept;

    private:
        bool m_saveModalOpen = false;
        bool m_loadModalOpen = false;

        std::string m_saveNameBuffer;
        std::vector<std::string> m_loadFileList;
        int m_loadSelection = -1;

        std::string m_lastMessage;
        bool m_lastMessageError = false;
    };
} // namespace NS::Editor