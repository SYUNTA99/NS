#pragma once

/// @file LevelFileBrowser.h
/// @brief 編集モード用 save / load dialog の ImGui 最小実装
///
/// @details Ctrl+S / Ctrl+O で `OpenSaveModal()` / `OpenLoadModal()` を呼ぶと
/// 次の OnRender で modal が描画される。 modal 内で OK が押されたら `Render()` の
/// 戻り値 `Result::action` で要求を caller に通知し、 実 I/O の `SaveLevelToFile` /
/// `LoadLevelFromFile` は caller の EditorMode 側が責任を持つ
/// 責任分離: 本クラスは UI と一時 buffer のみ管理、 SceneData / filesystem には触れない


namespace NS::Editor
{
    class LevelFileBrowser
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

        LevelFileBrowser(const LevelFileBrowser&) = delete;
        LevelFileBrowser& operator=(const LevelFileBrowser&) = delete;
        LevelFileBrowser(LevelFileBrowser&&) = delete;
        LevelFileBrowser& operator=(LevelFileBrowser&&) = delete;

        /// save modal を開く。 initialName が非空ならその名前を input に流し込む
        void OpenSaveModal(std::string_view initialName = {}) noexcept;
        void OpenLoadModal() noexcept;

        /// EditorLayer::OnRender 内で呼ぶ。 active modal を 1 フレーム描画し、
        /// OK 押下があれば `Result::action` 経由で caller に通知する
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
