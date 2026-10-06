#pragma once

#include "Editor/Timeline.h"
#include "NSlib/Core/NonCopyable.h"

#include <functional>
#include <memory>
#include <optional>

namespace NS::Obj
{
    class Scene;
    struct SceneView;
} // namespace NS::Obj

namespace NS::Editor
{
    //! 下見専用の場面。再生時計を持たず、指定されたフレームで止まる
    class TimelinePreview : public NS::NonCopyable
    {
    public:
        TimelinePreview();
        ~TimelinePreview();

        //! @brief 前の場面を捨て、開始状態を作る関数と範囲を受け取る
        //! @details create は範囲の先頭より 1 歩前の場面を作る
        //! 資産と Renderer はこの下見より長く生きること
        void Reset(std::function<std::unique_ptr<NS::Obj::Scene>()> create, TimelineFrameRange range);
        //! @brief 場面と生成関数を手放す
        void Clear() noexcept;
        //! @brief frame まで場面を進め、一時停止して返す
        //! @details 今のフレームより前へ戻る時だけ、開始から組み直す
        //! 同じフレームは更新しない。機器への入力と振動は中立
        //! @return 非所有の場面。生成できなかった場合は nullptr
        [[nodiscard]] NS::Obj::Scene* Seek(int frame);
        [[nodiscard]] NS::Obj::Scene* Scene() const noexcept { return m_scene.get(); }
        [[nodiscard]] std::optional<int> Frame() const noexcept { return m_frame; }
        //! @brief 現在のフレームを view の描画先へ描く
        //! @details 時間は進めない。描画先はこの呼び出しの中だけ借りる
        //! @return 描画を呼べた場合 true、それ以外の場合は false
        bool RenderView(const NS::Obj::SceneView& view);

    private:
        std::function<std::unique_ptr<NS::Obj::Scene>()> m_create;
        TimelineFrameRange m_range;
        std::unique_ptr<NS::Obj::Scene> m_scene;
        std::optional<int> m_frame;
    };
} // namespace NS::Editor
