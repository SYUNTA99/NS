#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Types.h"

#include <string>
#include <string_view>

namespace NS
{
    namespace Editor
    {
        //! レベルファイルを名前を付けて保存するモーダル UI。開くのはアセットの窓のシーンから
        class LevelFileBrowser : public NonCopyable
        {
        public:
            enum class Action : uint8
            {
                None,
                RequestSave,
            };

            struct Result
            {
                Action action = Action::None;
                std::string targetName;
            };

            LevelFileBrowser() = default;

            void OpenSaveModal(std::string_view initialName = {}) noexcept;

            //! モーダルを描き、操作の結果を Result で返す
            [[nodiscard]] Result Render() noexcept;

            void NotifySaveResult(bool ok, std::string_view message) noexcept;

        private:
            [[nodiscard]] Result RenderSaveModal() noexcept;

            bool m_saveModalOpen = false;

            std::string m_saveNameBuffer;

            std::string m_lastMessage;
            bool m_lastMessageError = false;
        };
    } // namespace Editor
} // namespace NS
