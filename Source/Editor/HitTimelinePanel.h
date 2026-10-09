#pragma once

// 当たりのタイムラインのパネル。事象の帯を並べて足し引きと欄の変更をし、選んだ条件の当たりを下見して、
// 置いた事象と実際に起きたフレームを重ねて見せる
// 出荷ビルドには載らない

#include "Editor/HitPreview.h"
#include "Editor/HitTimelineEdit.h"
#include "Editor/Timeline.h"
#include "Editor/TimelinePreview.h"
#include "Game/Level/HitTier.h"
#include "Game/Level/HitTimeline.h"
#include "NSlib/Core/NonCopyable.h"

#include <cstddef>
#include <optional>
#include <string>

class LevelEditorController;
struct ImDrawList;

namespace NS::Editor
{
    //! @brief 当たりのタイムラインを編集して下見するパネル
    //! @details 編集はパネルの中で持ち、変えるたびにタイムラインの置き場へ差し替えて (ファイルへは書かない)
    //! 下見し直す。 「保存」でファイルへ書き、「元に戻す」で最後に保存した中身を読み直す。編集中の場面は変えない
    class HitTimelinePanel : public NS::NonCopyable
    {
    public:
        HitTimelinePanel();

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

    private:
        // 条件の欄と下見の結果の行
        void RenderConditions(LevelEditorController& editor) noexcept;
        // 保存・元に戻す・事象を足す
        void RenderFileButtons() noexcept;
        // 事象ごとの帯と、下見で実際に始まったフレームの印と、再生の位置
        void RenderBands() noexcept;
        // 下見の揺れ・トラウマ・世界の速さを帯の下に折れ線で描く。横は帯と同じフレームの並び
        void RenderPreviewGraphs(ImDrawList& draw, const TimelineLayout& layout) noexcept;
        // 選んだ行の始まり・長さ・向きと、事象の欄
        void RenderSelectedRow() noexcept;
        // 再生・止め・コマ送り・速さ
        void RenderPlayback() noexcept;

        // 編集する段を tier にし、置き場から写しを取り直す
        void LoadWorking(GL::Level::HitTier tier);
        // 写しを置き場へ差し替え、下見し直しを頼む
        void ApplyWorking();
        // 写しの場面で下見する
        void RunPreview(LevelEditorController& editor);

        [[nodiscard]] TimelineFrameRange PlaybackRange() const noexcept;

        GL::Level::HitTier m_tier = GL::Level::HitTier::Center; // 編集している段
        GL::Level::HitTimeline m_working;                             // 編集している段の写し
        bool m_dirty = false;                                               // 保存していない変更があるか
        std::string m_status;                                               // 保存・読み直しの結果の 1 行

        HitPreviewDesc m_desc{};                  // 下見の条件
        HitPreviewResult m_result{};              // 直近の下見の結果
        nlohmann::json m_snapshot;                // 直近の下見に使った写し
        bool m_needsRun = false;                  // 下見し直しを頼まれているか
        std::optional<std::size_t> m_selectedRow; // 選んだ行。選んでいなければ空
        int m_addType = 0;                        // 足す事象の種類の番号 (HitEventValue の並び)
        TimelinePlayback m_playback;
        TimelinePreview m_preview;
        bool m_showPreview = false;
        bool m_panelVisible = false; // 直近の描画でパネルの中身が見えていたか
    };
} // namespace NS::Editor
