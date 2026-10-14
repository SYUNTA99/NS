#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Types.h"

#include <string>
#include <string_view>
#include <vector>

namespace NS::Editor
{
    //! @brief レベルファイルを保存・読込するモーダル UI
    class LevelFileBrowser : public NS::NonCopyable
    {
    public:
        enum class Action : NS::uint8
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

        void OpenSaveModal(std::string_view initialName = {}) noexcept;

        void OpenLoadModal() noexcept;

        //! @brief モーダルを描き、操作の結果を Result で返す
        [[nodiscard]] Result Render() noexcept;

        void NotifySaveResult(bool ok, std::string_view message) noexcept;

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
