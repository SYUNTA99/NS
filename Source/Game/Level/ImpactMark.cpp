#include "Game/Level/ImpactMark.h"

#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Windows/Clock.h"

#include <cmath>
#include <memory>
#include <utility>

namespace GL::Level
{
    ImpactMark::ImpactMark() noexcept
    {
        Root().SetScale(NS::Vector3{m_diameter, 1.0f, m_diameter});
    }

    void ImpactMark::OnInit()
    {
        NS::Obj::Model* model = CreateSubObj<NS::Obj::Model>(ModelSlot());
        model->SetMeshRef("shadowQuad");
        model->SetMaterialRef("shadow");
    }

    NS::Obj::Actor* ImpactMark::SpawnAt(NS::Obj::Scene* scene, const NS::Vector3& position)
    {
        if (scene == nullptr)
        {
            return nullptr;
        }

        // 部品は渡した後に OnInit が作る。ここで触るのは根だけ
        std::unique_ptr<ImpactMark> owned = std::make_unique<ImpactMark>();
        owned->Root().SetPosition(position);
        return scene->SpawnTransient(std::move(owned));
    }

    void ImpactMark::SetDiameter(float diameter) noexcept
    {
        if (std::isfinite(diameter) && diameter > 0.0f)
        {
            m_diameter = diameter;
            Root().SetScale(NS::Vector3{diameter, 1.0f, diameter});
        }
    }

    void ImpactMark::SetLifeSeconds(float seconds) noexcept
    {
        if (std::isfinite(seconds) && seconds >= 0.0f)
        {
            m_lifeSeconds = seconds;
        }
    }

    void ImpactMark::VisualStep()
    {
        NS::Obj::Actor::VisualStep();
        if (m_age >= m_lifeSeconds)
        {
            return;
        }
        m_age += NS::OS::FrameTimer::FixedDelta();
        float t = 1.0f;
        if (m_lifeSeconds > 0.0f)
        {
            t = NS::Clamp(m_age / m_lifeSeconds, 0.0f, 1.0f);
        }
        const float size = m_diameter * (1.0f - t);
        Root().SetScale(NS::Vector3{size, 1.0f, size});
        if (t >= 1.0f)
        {
            Kill();
        }
    }
} // namespace GL::Level
