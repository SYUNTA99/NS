#include "Editor/TimelinePreview.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Windows/Input.h"

#include <algorithm>
#include <utility>

namespace NS::Editor
{
    TimelinePreview::TimelinePreview() = default;
    TimelinePreview::~TimelinePreview()
    {
        Clear();
    }

    void TimelinePreview::Reset(std::function<std::unique_ptr<NS::Obj::Scene>()> create, TimelineFrameRange range)
    {
        Clear();
        m_create = std::move(create);
        m_range = range;
    }

    void TimelinePreview::Clear() noexcept
    {
        if (m_scene != nullptr)
        {
            const NS::OS::ScopedNeutralInput neutral;
            m_scene.reset();
        }
        m_create = {};
        m_frame.reset();
    }

    NS::Obj::Scene* TimelinePreview::Seek(int frame)
    {
        if (!m_create || m_range.last < m_range.first)
        {
            return nullptr;
        }
        frame = std::clamp(frame, m_range.first, m_range.last);
        const NS::OS::ScopedNeutralInput neutral;
        if (!m_scene || (m_frame.has_value() && frame < *m_frame))
        {
            m_scene.reset();
            m_frame.reset();
            m_scene = m_create();
            if (!m_scene)
            {
                return nullptr;
            }
        }
        int next = m_range.first;
        if (m_frame.has_value())
        {
            if (*m_frame == frame)
            {
                return m_scene.get();
            }
            next = *m_frame + 1;
        }
        m_scene->SetSimulationPaused(false);
        // 先頭も 1 歩後の姿。記録と描画で同じ添字を使うための並び
        for (int step = next; step <= frame; ++step)
        {
            NS::OS::Input::Get().Gamepad().StopVibration();
            m_scene->OnUpdate();
        }
        m_scene->SetSimulationPaused(true);
        m_frame = frame;
        return m_scene.get();
    }

    bool TimelinePreview::RenderView(const NS::Obj::SceneView& view)
    {
        if (!m_scene || !m_frame.has_value() || view.target == nullptr)
        {
            return false;
        }
        m_scene->SetSceneViews({view});
        m_scene->OnRender();
        m_scene->SetSceneViews({});
        return true;
    }
} // namespace NS::Editor
