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
        //! シーンで選んだアクターの .actor の動作を編集・下見する。編集は読んである .actor の文書へ即反映し、保存でファイルへ書く
        //! 下見は別シーンで 1 回当てて行い、編集中のシーンを変更しない
        class ActionTimelinePanel : public NonCopyable
        {
        public:
            ActionTimelinePanel();

            //! @brief パネルを描く
            //! @details プレイ中は編集の欄を出さない。下見の更新は Tick が行う
            void Render(LevelEditorController& editor) noexcept;

            //! @brief 配置の変更を調べ、下見の取り直しと再生時計を進める
            //! @details 毎描画フレームに呼ぶ。パネルも Game の下見も見えない間は配置を調べない
            //! プレイ中は下見を手放す
            void Tick(LevelEditorController& editor, float seconds);

            //! @brief 再生位置まで評価した下見を返す
            //! @details 取り直し待ちの間は直前の下見を表示する
            //! @return 編集中で下見表示が有効なら非所有の下見、それ以外は nullptr
            [[nodiscard]] TimelinePreview* Preview(LevelEditorController& editor);
            //! @brief 下見を手放して再生を止め、編集中の次の Tick で取り直す
            void ResetPreview() noexcept;

            //! 編集している .actor の名前。アクターを選ぶまでは空
            [[nodiscard]] const std::string& EditingActorFile() const noexcept { return m_actorFile; }
            //! 編集している動作。.actor に動作が無ければ nullptr
            [[nodiscard]] const ActorAction* EditingAction() const noexcept;

            //! @brief 保存していない変更のある .actor を全部ファイルへ書く
            //! @return 全部書けた時だけ true で、保存していない変更の記録を空にする。書けなかったファイルはパネルの 1 行に出す
            [[nodiscard]] bool SaveAll() noexcept;
            [[nodiscard]] bool HasUnsavedChanges() const noexcept { return !m_dirty.empty(); }

        private:
            // シーンで選んだアクターの .actor が編集中の物と違えば、その .actor を開き直す
            void FollowSelection(LevelEditorController& editor);
            void RenderConditions(LevelEditorController& editor) noexcept;
            void RenderActionButtons(LevelEditorController& editor) noexcept;
            // 動作の選び直しと、名前を付けた空の動作の足し
            void RenderActionChoice(LevelEditorController& editor) noexcept;
            // 変更した .actor を全部書く保存と、置き場からの読み直し
            void RenderFileButtons(LevelEditorController& editor) noexcept;
            // 再生の位置に事象か入り切りを足す
            void RenderAddButtons(LevelEditorController& editor) noexcept;
            // 事象と入り切りの帯と、下見で実際に始まったフレームのマーカーと、再生の位置
            void RenderBands() noexcept;
            // 下見の揺れ・トラウマ・世界の速さを帯の下に折れ線で描く。横は帯と同じフレームの並び
            void RenderPreviewGraphs(ImDrawList& draw, const TimelineLayout& layout) noexcept;
            void RenderSelectedRow(LevelEditorController& editor) noexcept;
            void RenderSelectedEvent(ActorActionEvent& event, bool& changed) noexcept;
            void RenderSelectedSensor(ActionSensorEvent& sensor, bool& changed) noexcept;
            void RenderPlayback() noexcept;

            // 読んである name の .actor の文書から動作を写す。動作の名前 action が無ければ最初の動作を開く
            void LoadWorking(std::string_view name, std::string_view action);
            // 写しを読んである .actor の文書へ書き、同じ .actor のシーンのアクターに読み直させ、下見し直しを頼む
            void ApplyWorking(LevelEditorController& editor);
            [[nodiscard]] ActorAction* WorkingAction() noexcept;
            void RunPreview(LevelEditorController& editor);

            // 下見の記録で、帯の時計の 0 に当たる frames の添字。編集している動作が走っていれば
            // その動作の 0 フレーム目、走っていなければ検知のフレーム
            [[nodiscard]] int PlaybackOrigin() const noexcept;
            [[nodiscard]] TimelineFrameRange PlaybackRange() const noexcept;

            std::string m_actorFile;                    // 編集している .actor の名前
            std::vector<ActorAction> m_actions;         // 編集している .actor の動作の写し
            std::size_t m_actionIndex = 0;              // 編集している動作の m_actions の番号
            std::set<std::string, std::less<>> m_dirty; // 保存していない変更のある .actor の名前
            char m_newName[64] = {};
            std::string m_status; // 保存・読み直しの結果の 1 行

            HitPreviewDesc m_desc{};                  // 下見の条件
            HitPreviewResult m_result{};              // 直近の下見の結果
            nlohmann::json m_snapshot;                // 直近の下見に使った写し
            bool m_needsRun = false;                  // 下見し直しを頼まれているか
            std::optional<std::size_t> m_selectedRow; // 選んだ行。事象の後に入り切りが並ぶ。選んでいなければ空
            int m_addType = 0;                        // 足す事象の種類の番号 (HitReactionItem の並び)
            TimelinePlayback m_playback;
            TimelinePreview m_preview;
            bool m_showPreview = false;
            bool m_panelVisible = false; // 直近の描画でパネルの中身が見えていたか
        };
    } // namespace Editor
} // namespace NS
