#pragma once

#include "Editor/HitPreview.h"
#include "Editor/Timeline.h"
#include "Editor/TimelinePreview.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/Action/ActorAction.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

class LevelEditorController;
struct ImDrawList;

namespace NS
{
    namespace Editor
    {
        class ActionTimelinePanel : public NonCopyable
        {
        public:
            ActionTimelinePanel();

            void Render(LevelEditorController& editor) noexcept;

            //! 毎描画フレームに呼ぶ。配置の変更を調べ、下見の取り直しと再生時計を進める（プレイ中は下見を手放す）
            void Tick(LevelEditorController& editor, float seconds);

            //! 再生位置まで評価した非所有の下見を返す（編集中かつ下見表示が有効でない場合は nullptr）
            [[nodiscard]] TimelinePreview* Preview(LevelEditorController& editor);
            void ResetPreview() noexcept;

            [[nodiscard]] const std::string& EditingActorFile() const noexcept { return m_actorFile; }
            [[nodiscard]] const ActorAction* EditingAction() const noexcept;

            //! 未保存の変更がある .actor をすべて保存する。すべて書けた時だけ未保存記録を空にして true を返す
            [[nodiscard]] bool SaveAll() noexcept;
            [[nodiscard]] bool HasUnsavedChanges() const noexcept { return !m_dirty.empty(); }

        private:
            void FollowSelection(LevelEditorController& editor);
            void RenderConditions(LevelEditorController& editor) noexcept;
            void RenderActionButtons(LevelEditorController& editor) noexcept;
            void RenderActionChoice(LevelEditorController& editor) noexcept;
            void RenderFileButtons(LevelEditorController& editor) noexcept;
            void RenderAddButtons(LevelEditorController& editor) noexcept;
            void RenderBands() noexcept;
            void RenderPreviewGraphs(ImDrawList& draw, const TimelineLayout& layout) noexcept;
            void RenderSelectedRow(LevelEditorController& editor) noexcept;
            void RenderSelectedEvent(ActorActionEvent& event, bool& changed) noexcept;
            void RenderSelectedSensor(ActionSensorEvent& sensor, bool& changed) noexcept;
            void RenderPlayback() noexcept;

            void LoadWorking(std::string_view name, std::string_view action);
            void ApplyWorking(LevelEditorController& editor);
            [[nodiscard]] ActorAction* WorkingAction() noexcept;
            void RunPreview(LevelEditorController& editor);

            [[nodiscard]] int PlaybackOrigin() const noexcept;
            [[nodiscard]] TimelineFrameRange PlaybackRange() const noexcept;

            std::string m_actorFile;
            std::vector<ActorAction> m_actions;
            std::size_t m_actionIndex = 0;
            std::set<std::string, std::less<>> m_dirty;
            char m_newName[64] = {};
            std::string m_status;

            HitPreviewDesc m_desc{};
            HitPreviewResult m_result{};
            nlohmann::json m_snapshot;
            bool m_needsRun = false;
            std::optional<std::size_t> m_selectedRow; // 事象の後ろにセンサーの入り切りを続けた通し行番号
            int m_addType = 0;
            TimelinePlayback m_playback;
            TimelinePreview m_preview;
            bool m_showPreview = false;
            bool m_panelVisible = false;
        };
    } // namespace Editor
} // namespace NS
