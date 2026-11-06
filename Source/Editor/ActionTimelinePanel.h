#pragma once

#include "Editor/Timeline.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/Action/ActorAction.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace NS
{
    namespace Editor
    {
        class LevelEditorController;

        class ActionTimelinePanel : public NonCopyable
        {
        public:
            void Render(LevelEditorController& editor) noexcept;

            [[nodiscard]] const std::string& EditingActorFile() const noexcept { return m_actorFile; }
            [[nodiscard]] const ActorAction* EditingAction() const noexcept;

            //! 未保存の変更がある .actor をすべて保存する。すべて書けた時だけ未保存記録を空にして true を返す
            [[nodiscard]] bool SaveAll() noexcept;
            [[nodiscard]] bool HasUnsavedChanges() const noexcept { return !m_dirty.empty(); }
            //! 最後の操作の結果としてパネルに出す文
            [[nodiscard]] const std::string& Status() const noexcept { return m_status; }

        private:
            void FollowSelection(LevelEditorController& editor);
            void RenderActionButtons(LevelEditorController& editor) noexcept;
            void RenderActionChoice(LevelEditorController& editor) noexcept;
            void RenderFileButtons(LevelEditorController& editor) noexcept;
            void RenderAddButtons(LevelEditorController& editor) noexcept;
            void RenderBands() noexcept;
            void RenderSelectedRow(LevelEditorController& editor) noexcept;
            void RenderSelectedEvent(ActorActionEvent& event, bool& changed) noexcept;
            void RenderSelectedSensor(const SceneActor* selected, ActionSensorEvent& sensor, bool& changed) noexcept;

            //! 未保存の .actor ごとに、動作だけをファイルの中身へ戻す。ファイルが読めない .actor は未保存のまま残す
            void RevertDirtyActions(LevelEditorController& editor);
            void LoadWorking(std::string_view name, std::string_view action);
            void ApplyWorking(LevelEditorController& editor);
            [[nodiscard]] ActorAction* WorkingAction() noexcept;

            std::string m_actorFile;
            std::vector<ActorAction> m_actions;
            std::size_t m_actionIndex = 0;
            std::set<std::string, std::less<>> m_dirty;
            char m_newName[64] = {};
            std::string m_status;

            std::optional<std::size_t> m_selectedRow; // 事象の後ろにセンサーの入り切りを続けた通し行番号
            int m_addType = 0;
            TimelinePlayback m_playback; // frame だけを使う。目盛りで動かし、事象と入り切りを足す始まりにする
        };
    } // namespace Editor
} // namespace NS
