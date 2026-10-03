#pragma once

// 当たりのタイムラインのパネル。事象の帯を並べて足し引きと欄の変更をし、選んだ条件の当たりを下見して、
// 置いた事象と実際に起きたフレームを重ねて見せる。選んだフレームの絵をゲームのカメラと横から 2 枚出す
// 出荷ビルドには載らない

#include "Editor/HitPreview.h"
#include "Editor/HitTimelineEdit.h"
#include "Editor/ViewportSurface.h"
#include "Game/Level/HitTier.h"
#include "Game/Level/HitTimeline.h"
#include "Runtime/Core/NonCopyable.h"

#include <cstddef>
#include <memory>
#include <string>

class LevelEditorController;
struct ImDrawList;

namespace NS::Obj
{
    class Scene;
} // namespace NS::Obj

namespace NS::Editor
{
    //! @brief 当たりのタイムラインを編集して下見するパネル
    //! @details 編集はパネルの中で持ち、変えるたびにタイムラインの置き場へ差し替えて (ファイルへは書かない)
    //! 下見し直す。 「保存」でファイルへ書き、「元に戻す」で最後に保存した中身を読み直す。編集中の場面は変えない
    class HitTimelinePanel : public NS::Core::NonCopyable
    {
    public:
        HitTimelinePanel();
        ~HitTimelinePanel();

        //! @brief パネルと、絵の 2 枚のパネルを描く
        //! @details プレイ中は下見しない (編集中の場面がプレイで動いているため)。下見し直しは、どのウィジェットも
        //! 触られていないフレームに走らせる (ドラッグの間に毎フレーム走らせない)
        void Render(LevelEditorController& editor) noexcept;

        //! 絵の 2 枚のパネルを出さないフレームに、描画先を作らないよう可視の印を下ろす
        void Suppress() noexcept;

        //! @brief 選んだフレームの下見の場面を、絵の 2 枚へ描く
        //! @details ImGui の描画の後、次のフレームの描画先が決まった所で呼ぶ。下見が無いか絵が隠れていれば何もしない
        void RenderPreview() noexcept;

        //! 描画先を破棄する。Renderer が非所有ポインタを踏まないよう外した後に呼ぶ
        void ReleaseTargets() noexcept;

    private:
        // 条件の欄と下見の結果の行
        void RenderConditions(LevelEditorController& editor) noexcept;
        // 保存・元に戻す・事象を足す
        void RenderFileButtons() noexcept;
        // 事象ごとの帯と、下見で実際に始まったフレームの印と、再生の位置
        void RenderBands() noexcept;
        // 下見の揺れ・トラウマ・世界の速さを帯の下に折れ線で描く。横は帯と同じフレームの並び
        void RenderPreviewGraphs(
            ImDrawList& draw, int firstClock, float frameWidth, float totalWidth, float rowHeight) noexcept;
        // 選んだ行の始まり・長さ・向きと、事象の欄
        void RenderSelectedRow() noexcept;
        // 再生・止め・コマ送り・速さ
        void RenderPlayback() noexcept;
        // 絵の 2 枚のパネル
        void RenderImages() noexcept;

        // 編集する段を tier にし、置き場から写しを取り直す
        void LoadWorking(NS::Game::Level::HitTier tier);
        // 写しを置き場へ差し替え、下見し直しを頼む
        void ApplyWorking();
        // 写しの場面で下見する
        void RunPreview(LevelEditorController& editor);
        // 選んだフレームの場面を用意する。前へ進むだけなら組み直さない
        void PrepareSceneAt(int frameIndex);
        // 描く場面と写しを捨てる
        void DropPreview() noexcept;

        NS::Game::Level::HitTier m_tier = NS::Game::Level::HitTier::Center; // 編集している段
        NS::Game::Level::HitTimeline m_working;                             // 編集している段の写し
        bool m_dirty = false;                                               // 保存していない変更があるか
        std::string m_status;                                               // 保存・読み直しの結果の 1 行

        HitPreviewDesc m_desc{};       // 下見の条件
        HitPreviewResult m_result{};   // 直近の下見の結果
        nlohmann::json m_snapshot;     // 直近の下見に使った写し
        bool m_needsRun = false;       // 下見し直しを頼まれているか
        std::size_t m_selectedRow = 0; // 選んだ行。行が無ければ使わない
        bool m_hasSelectedRow = false; // 行を選んでいるか
        int m_addType = 0;             // 足す事象の種類の番号 (HitEventValue の並び)
        HitPreviewPlayback m_playback; // 再生の位置

        std::unique_ptr<NS::Obj::Scene> m_scene; // 選んだフレームまで進めた下見の場面
        int m_sceneFrame = -1;                   // m_scene を進めたフレーム。無ければ -1
        ViewportSurface m_gameSurface;           // ゲームのカメラから見た絵
        ViewportSurface m_sideSurface;           // 横から見た絵
        bool m_sideFlipped = false;              // 横の絵を突進の向きの左から見るか。既定は右から
    };
} // namespace NS::Editor
